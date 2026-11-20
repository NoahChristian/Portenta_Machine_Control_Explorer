/*
 * CAN on FDCAN1. Pin mapping, STB and the FD decision are in canbus.h.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 */

#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>
/* For GPIOH->IDR: the TX pin is sampled directly, see pmc_can_state_read(). */
#include <stm32_ll_system.h>

#include "canbus.h"

LOG_MODULE_REGISTER(pmc_can, LOG_LEVEL_INF);

#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(fdcan1))

#define PMC_HAVE_CAN 1

static const struct device *const dev = DEVICE_DT_GET(DT_NODELABEL(fdcan1));

/*
 * STB on PA13. Raw port and pin rather than a devicetree node, matching the
 * output latch in do_interlock.c and for the same reason: the generated
 * channel table that will own every pin on this carrier does not exist yet,
 * and inventing a one-off binding here would create a second place a pin
 * number lives. This comment is the marker for moving it.
 */
static const struct device *const stb_port = DEVICE_DT_GET(DT_NODELABEL(gpioa));
#define STB_PIN 13

static struct k_mutex lock;
static bool  started;
static bool  loopback;
static uint8_t stb_level;
static uint32_t bitrate = DT_PROP_OR(DT_NODELABEL(fdcan1), bitrate, 500000);

static atomic_t tx_ok, tx_fail, tx_no_ack, tx_queued, tx_reject, rx_frames;

/* The receive window. Written from the RX callback, read by the panel. */
static struct pmc_can_frame ring[PMC_CAN_LOG];
static uint32_t ring_head;
static struct k_spinlock ring_lock;

/* Set once at init; see pmc_can_rx_tap_set() and the note in canbus.h. */
static pmc_can_rx_tap_t rx_tap;

void pmc_can_rx_tap_set(pmc_can_rx_tap_t fn)
{
	rx_tap = fn;
}

static void rx_cb(const struct device *d, struct can_frame *f, void *user)
{
	k_spinlock_key_t key;
	struct pmc_can_frame *slot;
	struct pmc_can_frame copy;

	ARG_UNUSED(d);
	ARG_UNUSED(user);

	atomic_inc(&rx_frames);

	key = k_spin_lock(&ring_lock);
	slot = &ring[ring_head % PMC_CAN_LOG];
	ring_head++;

	slot->id = f->id;
	slot->ext = (f->flags & CAN_FRAME_IDE) ? 1U : 0U;
	slot->rtr = (f->flags & CAN_FRAME_RTR) ? 1U : 0U;
	slot->dlc = can_dlc_to_bytes(f->dlc);
	if (slot->dlc > sizeof(slot->data)) {
		slot->dlc = sizeof(slot->data);
	}
	memcpy(slot->data, f->data, slot->dlc);
	slot->stamp = k_uptime_get();
	copy = *slot;
	k_spin_unlock(&ring_lock, key);

	/*
	 * A COPY, AND OUTSIDE THE LOCK. The ring entry is reused after
	 * PMC_CAN_LOG more frames and the tap's consumer runs on its own
	 * thread, so handing it the slot would hand it storage that can change
	 * under it. Called with no lock held because this runs in interrupt
	 * context already and holding a spinlock across somebody else's code
	 * is how one subsystem's mistake becomes another's.
	 */
	if (rx_tap != NULL) {
		rx_tap(&copy);
	}
}

/*
 * A SINGLE-NODE BUS CANNOT COMPLETE A TRANSMISSION, and that is the first
 * thing anyone testing CAN alone runs into. With no second node to
 * acknowledge, the controller retries the frame forever and the send times
 * out; the error is -ETIMEDOUT or -EIO depending on where it gives up.
 *
 * Counted separately from a real failure so the panel can say "nothing
 * acknowledged this" rather than "the controller is broken", because those
 * look identical from a counter and need completely different responses.
 */
static void tx_done(const struct device *d, int error, void *user)
{
	ARG_UNUSED(d);
	ARG_UNUSED(user);

	if (error == 0) {
		atomic_inc(&tx_ok);
	} else if (error == -ETIMEDOUT || error == -ENETUNREACH) {
		atomic_inc(&tx_no_ack);
	} else {
		atomic_inc(&tx_fail);
	}
}

static int start_locked(void)
{
	int rc;

	rc = can_set_mode(dev, loopback ? CAN_MODE_LOOPBACK : CAN_MODE_NORMAL);
	if (rc != 0) {
		LOG_ERR("can_set_mode: %d", rc);
		return rc;
	}
	rc = can_set_bitrate(dev, bitrate);
	if (rc != 0) {
		LOG_ERR("can_set_bitrate(%u): %d", bitrate, rc);
		return rc;
	}
	rc = can_start(dev);
	if (rc != 0 && rc != -EALREADY) {
		LOG_ERR("can_start: %d", rc);
		return rc;
	}
	started = true;
	return 0;
}

static int stop_locked(void)
{
	const int rc = can_stop(dev);

	if (rc == 0 || rc == -EALREADY) {
		started = false;
		return 0;
	}
	return rc;
}

int pmc_can_init(void)
{
	struct can_filter all = {
		.id = 0,
		.mask = 0,           /* mask 0 matches every identifier */
		.flags = 0,
	};
	int rc;

	k_mutex_init(&lock);

	if (!device_is_ready(dev)) {
		LOG_ERR("FDCAN1 not ready");
		return -ENODEV;
	}

	if (device_is_ready(stb_port)) {
		/*
		 * Driven LOW at init. On the common convention that is the
		 * transceiver's normal, listening mode - but see canbus.h:
		 * the polarity is not confirmed on this carrier, and the panel
		 * reports the level rather than a meaning.
		 */
		rc = gpio_pin_configure(stb_port, STB_PIN, GPIO_OUTPUT_INACTIVE);
		if (rc == 0) {
			stb_level = 0U;
			LOG_WRN("CAN STB driving PA13 - SWDIO is gone until "
				"the next reset");
		} else {
			LOG_ERR("CAN STB configure failed (%d)", rc);
		}
	}

	rc = can_add_rx_filter(dev, rx_cb, NULL, &all);
	if (rc < 0) {
		LOG_ERR("can_add_rx_filter: %d", rc);
		return rc;
	}

	k_mutex_lock(&lock, K_FOREVER);
	rc = start_locked();
	k_mutex_unlock(&lock);

	if (rc == 0) {
		LOG_INF("CAN up at %u bit/s, classic, accepting every id",
			bitrate);
	}
	return rc;
}

void pmc_can_state_read(struct pmc_can_state *out)
{
	enum can_state s = CAN_STATE_ERROR_ACTIVE;
	struct can_bus_err_cnt cnt = { 0 };

	if (out == NULL) {
		return;
	}
	memset(out, 0, sizeof(*out));
	out->present = true;

	if (!device_is_ready(dev)) {
		return;
	}

	(void)can_get_state(dev, &s, &cnt);

	out->started = started;
	out->loopback = loopback;
	out->stb_level = stb_level;
	out->bitrate = bitrate;
	out->bus_state = (int)s;
	out->tx_err_cnt = cnt.tx_err_cnt;
	out->rx_err_cnt = cnt.rx_err_cnt;
	out->tx_ok = (uint32_t)atomic_get(&tx_ok);
	out->tx_fail = (uint32_t)atomic_get(&tx_fail);
	out->tx_reject = (uint32_t)atomic_get(&tx_reject);
	out->tx_no_ack = (uint32_t)atomic_get(&tx_no_ack);
	out->tx_queued = (uint32_t)atomic_get(&tx_queued);
	out->rx_frames = (uint32_t)atomic_get(&rx_frames);

	/*
	 * SAMPLE THE TX PIN. GPIOH->IDR reflects the pad even in alternate
	 * function mode, so a read loop sees the CAN transmitter driving it.
	 * A frame at 500 kbit/s is about 200 us; a lone node retransmitting
	 * is therefore toggling this pin almost continuously, and an idle one
	 * holds it recessive (high). Both levels appearing is the answer.
	 */
	{
		unsigned ones = 0;

		for (unsigned i = 0; i < 400U; i++) {
			if (GPIOH->IDR & BIT(13)) {
				ones++;
			}
		}
		out->tx_pin_sampled = 1U;
		out->tx_pin_toggling = (ones != 0U && ones != 400U) ? 1U : 0U;
	}
}

size_t pmc_can_log_read(struct pmc_can_frame *out, size_t cap)
{
	k_spinlock_key_t key;
	size_t n = 0;

	if (out == NULL || cap == 0) {
		return 0;
	}

	key = k_spin_lock(&ring_lock);
	const uint32_t have = (ring_head < PMC_CAN_LOG) ? ring_head
							: PMC_CAN_LOG;

	/* Newest first, which is the order a person reads a bus window in. */
	for (uint32_t i = 0; i < have && n < cap; i++) {
		out[n++] = ring[(ring_head - 1 - i) % PMC_CAN_LOG];
	}
	k_spin_unlock(&ring_lock, key);
	return n;
}

/*
 * The receive window as a cursor. `ring_head` is already a monotonically
 * increasing frame index, so it IS the cursor - there is no second counter to
 * keep in step with the ring.
 */
size_t pmc_can_rx_read(uint64_t from, struct pmc_can_frame *out, size_t cap,
		       uint64_t *next, bool *lost)
{
	k_spinlock_key_t key;
	size_t n = 0;

	if (out == NULL || next == NULL || lost == NULL) {
		return 0;
	}

	key = k_spin_lock(&ring_lock);

	const uint64_t total = ring_head;
	const uint64_t oldest = (total > PMC_CAN_LOG) ? total - PMC_CAN_LOG : 0;

	*lost = false;
	if (from < oldest) {
		/* The frames asked for are gone. Saying so is the point: a
		   monitor that silently resumes looks like a quiet bus. */
		*lost = true;
		from = oldest;
	}

	for (uint64_t i = from; i < total && n < cap; i++) {
		out[n++] = ring[i % PMC_CAN_LOG];
	}
	*next = from + n;

	k_spin_unlock(&ring_lock, key);
	return n;
}

uint64_t pmc_can_rx_cursor(void)
{
	const k_spinlock_key_t key = k_spin_lock(&ring_lock);
	const uint64_t at = ring_head;

	k_spin_unlock(&ring_lock, key);
	return at;
}

/* ------------------------------------------------------- the repeater --- */

static struct can_frame rep_frame;
static uint32_t         rep_period_ms;
static atomic_t         rep_sent;

static void rep_work_fn(struct k_work *w);
static K_WORK_DELAYABLE_DEFINE(rep_work, rep_work_fn);

static void rep_work_fn(struct k_work *w)
{
	ARG_UNUSED(w);

	if (rep_period_ms == 0U || !started) {
		return;
	}
	/*
	 * No completion callback. On a bus with no second node the previous
	 * frame is still in flight when the next period comes round, and the
	 * outcome of a repeat is not what this is for - the state line already
	 * reports frames in flight and the controller's error counters.
	 */
	(void)can_send(dev, &rep_frame, K_NO_WAIT, NULL, NULL);
	atomic_inc(&rep_sent);
	k_work_reschedule(&rep_work, K_MSEC(rep_period_ms));
}

bool pmc_can_repeat(uint32_t id, bool ext, const uint8_t *data, uint8_t len,
		    uint32_t period_ms, char *detail, size_t cap)
{
	if (period_ms == 0U) {
		rep_period_ms = 0U;
		(void)k_work_cancel_delayable(&rep_work);
		(void)snprintk(detail, cap, "repeat stopped after %u frames",
			       (unsigned)atomic_get(&rep_sent));
		return true;
	}
	if (period_ms < 5U) {
		(void)snprintk(detail, cap,
			       "refused: %u ms is faster than this is for - "
			       "5 ms minimum", period_ms);
		return false;
	}
	if (len > 8U) {
		(void)snprintk(detail, cap, "refused: 8 bytes maximum");
		return false;
	}
	if (!started) {
		(void)snprintk(detail, cap, "refused: the controller is "
					    "stopped");
		return false;
	}

	memset(&rep_frame, 0, sizeof(rep_frame));
	rep_frame.id = id;
	rep_frame.dlc = can_bytes_to_dlc(len);
	rep_frame.flags = ext ? CAN_FRAME_IDE : 0;
	if (len > 0U && data != NULL) {
		memcpy(rep_frame.data, data, len);
	}

	atomic_set(&rep_sent, 0);
	rep_period_ms = period_ms;
	k_work_reschedule(&rep_work, K_NO_WAIT);

	(void)snprintk(detail, cap, "repeating 0x%x every %u ms%s",
		       (unsigned)id, period_ms,
		       loopback ? " (loopback - it comes straight back)" : "");
	return true;
}

void pmc_can_repeat_get(uint32_t *period_ms, uint32_t *sent)
{
	if (period_ms != NULL) {
		*period_ms = rep_period_ms;
	}
	if (sent != NULL) {
		*sent = (uint32_t)atomic_get(&rep_sent);
	}
}

bool pmc_can_send(uint32_t id, bool ext, const uint8_t *data, uint8_t len,
		  char *detail, size_t cap)
{
	struct can_frame f = { 0 };
	int rc;

	if (!device_is_ready(dev)) {
		(void)snprintk(detail, cap, "refused: no CAN controller");
		return false;
	}
	if (len > 8U) {
		(void)snprintk(detail, cap,
			       "refused: classic CAN carries 8 bytes, not %u",
			       (unsigned)len);
		return false;
	}
	if (!ext && id > 0x7ffU) {
		(void)snprintk(detail, cap,
			       "refused: 0x%x needs a 29-bit identifier",
			       (unsigned)id);
		return false;
	}
	if (id > 0x1fffffffU) {
		(void)snprintk(detail, cap, "refused: 0x%x is not a CAN id",
			       (unsigned)id);
		return false;
	}

	f.id = id;
	f.dlc = can_bytes_to_dlc(len);
	f.flags = ext ? CAN_FRAME_IDE : 0;
	if (len > 0 && data != NULL) {
		memcpy(f.data, data, len);
	}

	k_mutex_lock(&lock, K_FOREVER);
	if (!started) {
		k_mutex_unlock(&lock);
		(void)snprintk(detail, cap, "refused: the controller is "
					    "stopped");
		return false;
	}
	/*
	 * Non-blocking with a completion callback rather than a blocking send.
	 * On a bus with nobody listening the frame is retried until it times
	 * out, and blocking the caller - which may be the one HTTP server
	 * thread - for that whole time is the mistake the pulse code already
	 * made once.
	 */
	rc = can_send(dev, &f, K_NO_WAIT, tx_done, NULL);
	if (rc == 0) {
		atomic_inc(&tx_queued);
	}
	k_mutex_unlock(&lock);

	if (rc != 0) {
		/*
		 * REJECTED, NOT FAILED, and they are counted apart - see
		 * canbus.h. This one was never queued, so counting it as a
		 * failure made "queued minus the outcomes" go negative.
		 */
		atomic_inc(&tx_reject);
		(void)snprintk(detail, cap, "refused: can_send %d%s", rc,
			       rc == -EAGAIN ? " (no free mailbox)" : "");
		return false;
	}

	(void)snprintk(detail, cap,
		       "queued 0x%x, %u byte%s%s - delivery is reported by the "
		       "counters, not here",
		       (unsigned)id, (unsigned)len, len == 1 ? "" : "s",
		       loopback ? ", loopback" : "");
	return true;
}

bool pmc_can_set_loopback(bool on, char *detail, size_t cap)
{
	int rc;

	k_mutex_lock(&lock, K_FOREVER);
	(void)stop_locked();
	loopback = on;
	rc = start_locked();
	k_mutex_unlock(&lock);

	if (rc != 0) {
		(void)snprintk(detail, cap, "refused: restart failed (%d)", rc);
		return false;
	}
	(void)snprintk(detail, cap, "%s",
		       on ? "loopback: INTERNAL - measured, the TX pin stays "
			    "recessive, so nothing reaches the transceiver"
			  : "normal: frames go out on the bus and need a "
			    "second node to acknowledge them");
	return true;
}

bool pmc_can_set_bitrate(uint32_t bps, char *detail, size_t cap)
{
	int rc;

	if (bps < 10000U || bps > 1000000U) {
		(void)snprintk(detail, cap,
			       "refused: %u is outside 10k..1M", bps);
		return false;
	}

	k_mutex_lock(&lock, K_FOREVER);
	(void)stop_locked();
	bitrate = bps;
	rc = start_locked();
	k_mutex_unlock(&lock);

	if (rc != 0) {
		(void)snprintk(detail, cap,
			       "refused: %u not achievable here (%d)", bps, rc);
		return false;
	}
	(void)snprintk(detail, cap, "bitrate %u", bps);
	return true;
}

bool pmc_can_stb(bool high, char *detail, size_t cap)
{
	if (!device_is_ready(stb_port)) {
		(void)snprintk(detail, cap, "refused: no STB pin");
		return false;
	}
	(void)gpio_pin_set(stb_port, STB_PIN, high ? 1 : 0);
	stb_level = high ? 1U : 0U;

	(void)snprintk(detail, cap,
		       "STB driven %s. Which mode that selects is NOT "
		       "confirmed on this carrier - watch whether frames move.",
		       high ? "HIGH" : "LOW");
	return true;
}

bool pmc_can_recover(char *detail, size_t cap)
{
	int rc;

	k_mutex_lock(&lock, K_FOREVER);
	(void)stop_locked();
	rc = start_locked();
	k_mutex_unlock(&lock);

	if (rc != 0) {
		(void)snprintk(detail, cap, "refused: restart failed (%d)", rc);
		return false;
	}
	(void)snprintk(detail, cap, "controller restarted");
	return true;
}

/* ---------------------------------------------------------------- shell --- */

static const char *state_name(int s)
{
	switch (s) {
	case CAN_STATE_ERROR_ACTIVE:  return "ERROR_ACTIVE (healthy)";
	case CAN_STATE_ERROR_WARNING: return "ERROR_WARNING";
	case CAN_STATE_ERROR_PASSIVE: return "ERROR_PASSIVE";
	case CAN_STATE_BUS_OFF:       return "BUS_OFF - will not transmit";
	case CAN_STATE_STOPPED:       return "STOPPED";
	default:                      return "unrecognized";
	}
}

static int cmd_can(const struct shell *sh, size_t argc, char **argv)
{
	char detail[128];

	if (argc >= 2 && strcmp(argv[1], "loopback") == 0) {
		const bool on = (argc < 3) || (strcmp(argv[2], "off") != 0);

		if (!pmc_can_set_loopback(on, detail, sizeof(detail))) {
			shell_error(sh, "%s", detail);
			return -EIO;
		}
		shell_print(sh, "%s", detail);
		return 0;
	}
	if (argc == 3 && strcmp(argv[1], "bitrate") == 0) {
		if (!pmc_can_set_bitrate((uint32_t)strtoul(argv[2], NULL, 0),
					 detail, sizeof(detail))) {
			shell_error(sh, "%s", detail);
			return -EIO;
		}
		shell_print(sh, "%s", detail);
		return 0;
	}
	if (argc == 3 && strcmp(argv[1], "stb") == 0) {
		(void)pmc_can_stb(strcmp(argv[2], "high") == 0, detail,
				  sizeof(detail));
		shell_print(sh, "%s", detail);
		return 0;
	}
	/*
	 * `can txprobe` - is the TX pin driven IN THIS MODE?
	 *
	 * The question this exists for: Zephyr's CAN_MODE_LOOPBACK sets
	 * TEST.LBCK and leaves CCCR.MON clear, which by the M_CAN
	 * specification is EXTERNAL loop back - the TX pin driven, the RX pin
	 * disregarded. Reading that out of the driver is not the same as
	 * seeing it, and sampling the pin from the shell AFTER a send proves
	 * nothing: a frame at 500 kbit/s is about 200 us and the reply takes
	 * milliseconds, so the pin is always idle again by then. The first
	 * attempt did exactly that and read "static", which was a statement
	 * about the timing and not about the mode.
	 *
	 * So the send and the sampling happen in the same loop here, with
	 * nothing in between. If any sample catches the pin low, the
	 * transmitter is driving it in whatever mode is current.
	 */
	if (argc == 2 && strcmp(argv[1], "txprobe") == 0) {
		unsigned lows = 0;
		unsigned total = 0;

		if (!started) {
			shell_error(sh, "the controller is stopped");
			return -EIO;
		}

		for (unsigned f = 0; f < 48U; f++) {
			struct can_frame probe = {
				.id = 0x7a0U + (f & 0xfU),
				.dlc = can_bytes_to_dlc(8),
				.flags = 0,
			};

			memset(probe.data, 0x55, 8);   /* 0x55 = alternating */
			(void)can_send(dev, &probe, K_NO_WAIT, NULL, NULL);

			for (unsigned k = 0; k < 3000U; k++) {
				if ((GPIOH->IDR & BIT(13)) == 0U) {
					lows++;
				}
				total++;
			}
		}

		shell_print(sh, "mode     %s",
			    loopback ? "LOOPBACK" : "normal");
		shell_print(sh, "sampled  %u times across 48 frames", total);
		shell_print(sh, "PH13 low %u", lows);
		shell_print(sh, "");
		if (lows > 0U) {
			shell_print(sh, "THE TX PIN IS DRIVEN in this mode, so "
					"frames physically leave");
			shell_print(sh, "the chip and reach the transceiver. "
					"This is EXTERNAL loop back.");
		} else {
			shell_print(sh, "The TX pin never left recessive, so "
					"nothing reached the");
			shell_print(sh, "transceiver. This is INTERNAL loop "
					"back and it tests the");
			shell_print(sh, "controller only.");
		}
		return 0;
	}

	if (argc == 2 && strcmp(argv[1], "recover") == 0) {
		(void)pmc_can_recover(detail, sizeof(detail));
		shell_print(sh, "%s", detail);
		return 0;
	}
	/*
	 * `can rep <ms> <id> [bytes]` - the traffic source for a two-console
	 * session: one window makes the bus busy, another watches it. `can
	 * rep 0` stops.
	 */
	if (argc >= 2 && strcmp(argv[1], "rep") == 0) {
		uint8_t data[8];
		uint8_t len = 0;
		uint32_t ms = (argc >= 3) ? (uint32_t)strtoul(argv[2], NULL, 0)
					  : 0U;
		uint32_t id = (argc >= 4) ? (uint32_t)strtoul(argv[3], NULL, 0)
					  : 0x100U;

		for (size_t i = 4; i < argc && len < sizeof(data); i++) {
			data[len++] = (uint8_t)strtoul(argv[i], NULL, 16);
		}
		if (!pmc_can_repeat(id, id > 0x7ffU, data, len, ms, detail,
				    sizeof(detail))) {
			shell_error(sh, "%s", detail);
			return -EINVAL;
		}
		shell_print(sh, "%s", detail);
		return 0;
	}

	if (argc >= 3 && strcmp(argv[1], "send") == 0) {
		const uint32_t id = (uint32_t)strtoul(argv[2], NULL, 0);
		uint8_t data[8];
		uint8_t len = 0;

		for (size_t i = 3; i < argc && len < sizeof(data); i++) {
			data[len++] = (uint8_t)strtoul(argv[i], NULL, 16);
		}
		if (!pmc_can_send(id, id > 0x7ffU, data, len, detail,
				  sizeof(detail))) {
			shell_error(sh, "%s", detail);
			return -EIO;
		}
		shell_print(sh, "%s", detail);
		return 0;
	}

	struct pmc_can_state s;
	struct pmc_can_frame log[PMC_CAN_LOG];
	size_t n;

	pmc_can_state_read(&s);
	shell_print(sh, "state    %s", state_name(s.bus_state));
	shell_print(sh, "mode     %s, %u bit/s, %s",
		    s.loopback ? "LOOPBACK (no wire)" : "normal",
		    s.bitrate, s.started ? "started" : "stopped");
	shell_print(sh, "STB      PA13 driven %s (meaning unconfirmed)",
		    s.stb_level ? "HIGH" : "LOW");
	shell_print(sh, "errors   tx %u  rx %u  (controller's own counters)",
		    s.tx_err_cnt, s.rx_err_cnt);

	/* Rejected sends are NOT in this subtraction; they never reached the
	   queue. That mistake printed 4,294,966,629 once. See canbus.h. */
	const uint32_t inflight = s.tx_queued -
				  (s.tx_ok + s.tx_no_ack + s.tx_fail);

	shell_print(sh, "frames   tx %u queued, %u ok, %u failed, "
			"**%u STILL IN FLIGHT**; rx %u",
		    s.tx_queued, s.tx_ok, s.tx_fail, inflight, s.rx_frames);
	if (s.tx_reject != 0U) {
		shell_print(sh, "rejected %u send%s the controller would not "
				"take - with one mailbox held by a frame",
			    s.tx_reject, s.tx_reject == 1U ? "" : "s");
		shell_print(sh, "         nobody acknowledges, every later "
				"send is refused immediately.");
	}
	shell_print(sh, "TX pin   PH13 %s",
		    s.tx_pin_toggling ? "TOGGLING - the transceiver is being "
					"driven"
				      : "static - nothing is transmitting");

	if (inflight > 0U && s.bus_state == CAN_STATE_ERROR_PASSIVE) {
		shell_print(sh, "");
		shell_print(sh, "That is a frame NOBODY ACKNOWLEDGED. The ACK "
				"bit has to be driven by");
		shell_print(sh, "a different node; a transmitter sends "
				"recessive there and cannot");
		shell_print(sh, "answer itself, so no amount of wiring CANH to "
				"CANL fixes it. The");
		shell_print(sh, "controller parks at ERROR_PASSIVE, tx error "
				"128, and retransmits");
		shell_print(sh, "forever - which is why the TX pin above is "
				"still moving.");
	}

	n = pmc_can_log_read(log, ARRAY_SIZE(log));
	if (n > 0) {
		shell_print(sh, "");
		shell_print(sh, "last %u received, newest first:", (unsigned)n);
		for (size_t i = 0; i < n; i++) {
			char hex[3 * 8 + 1] = { 0 };
			size_t p = 0;

			for (uint8_t b = 0; b < log[i].dlc; b++) {
				p += (size_t)snprintk(hex + p, sizeof(hex) - p,
						      "%02x ", log[i].data[b]);
			}
			shell_print(sh, "  %8lld ms  %s0x%-8x  %u  %s",
				    log[i].stamp, log[i].ext ? "x" : " ",
				    log[i].id, log[i].dlc, hex);
		}
	}

	shell_print(sh, "");
	shell_print(sh, "  can loopback [off]   internal loopback - proves the");
	shell_print(sh, "                       controller with no bus at all");
	shell_print(sh, "  can send <id> [bytes in hex]");
	shell_print(sh, "  can rep <ms> <id> [bytes]   repeat; 'can rep 0' stops");
	shell_print(sh, "  can txprobe          is the TX pin driven in this mode");
	shell_print(sh, "  can bitrate <bps>    10k..1M");
	shell_print(sh, "  can stb high|low     the transceiver's standby pin");
	shell_print(sh, "  can recover          restart after BUS_OFF");
	shell_print(sh, "");
	shell_print(sh, "ALONE ON A BUS NOTHING ACKNOWLEDGES A FRAME, so a send");
	shell_print(sh, "in normal mode counts as unacknowledged and the error");
	shell_print(sh, "counter climbs. That is the bus answering honestly, not");
	shell_print(sh, "a fault. Use loopback to test the controller by itself.");
	return 0;
}

SHELL_CMD_ARG_REGISTER(can, NULL,
		       "CAN state, counters and a receive window.",
		       cmd_can, 1, 11);

#else  /* fdcan1 disabled */

int pmc_can_init(void) { return -ENODEV; }

void pmc_can_state_read(struct pmc_can_state *out)
{
	if (out != NULL) {
		memset(out, 0, sizeof(*out));
	}
}

size_t pmc_can_log_read(struct pmc_can_frame *out, size_t cap)
{
	ARG_UNUSED(out);
	ARG_UNUSED(cap);
	return 0;
}

size_t pmc_can_rx_read(uint64_t from, struct pmc_can_frame *out, size_t cap,
		       uint64_t *next, bool *lost)
{
	ARG_UNUSED(from); ARG_UNUSED(out); ARG_UNUSED(cap);
	if (next != NULL) { *next = 0; }
	if (lost != NULL) { *lost = false; }
	return 0;
}

uint64_t pmc_can_rx_cursor(void) { return 0; }

void pmc_can_rx_tap_set(pmc_can_rx_tap_t fn) { ARG_UNUSED(fn); }

bool pmc_can_repeat(uint32_t id, bool ext, const uint8_t *data, uint8_t len,
		    uint32_t period_ms, char *detail, size_t cap)
{
	ARG_UNUSED(id); ARG_UNUSED(ext); ARG_UNUSED(data);
	ARG_UNUSED(len); ARG_UNUSED(period_ms);
	(void)snprintk(detail, cap, "refused: no CAN in this build");
	return false;
}

void pmc_can_repeat_get(uint32_t *period_ms, uint32_t *sent)
{
	if (period_ms != NULL) { *period_ms = 0; }
	if (sent != NULL) { *sent = 0; }
}

bool pmc_can_send(uint32_t id, bool ext, const uint8_t *data, uint8_t len,
		  char *detail, size_t cap)
{
	ARG_UNUSED(id); ARG_UNUSED(ext); ARG_UNUSED(data); ARG_UNUSED(len);
	(void)snprintk(detail, cap, "refused: no CAN in this build");
	return false;
}

bool pmc_can_set_loopback(bool on, char *detail, size_t cap)
{
	ARG_UNUSED(on);
	(void)snprintk(detail, cap, "refused: no CAN in this build");
	return false;
}

bool pmc_can_set_bitrate(uint32_t bps, char *detail, size_t cap)
{
	ARG_UNUSED(bps);
	(void)snprintk(detail, cap, "refused: no CAN in this build");
	return false;
}

bool pmc_can_stb(bool high, char *detail, size_t cap)
{
	ARG_UNUSED(high);
	(void)snprintk(detail, cap, "refused: no CAN in this build");
	return false;
}

bool pmc_can_recover(char *detail, size_t cap)
{
	(void)snprintk(detail, cap, "refused: no CAN in this build");
	return false;
}

#endif /* fdcan1 */
