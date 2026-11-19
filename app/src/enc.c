/*
 * The two incremental encoder inputs. Why software and not a timer: enc.h.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 */

#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>

#include "enc.h"

LOG_MODULE_REGISTER(pmc_enc, LOG_LEVEL_INF);

/*
 * THE QUADRATURE TABLE, indexed by (previous << 2) | current, where each
 * half is (A << 1) | B. Sixteen entries because there are four states and
 * four possible successors.
 *
 *   +1  the pair advanced one step in the A-leads-B direction
 *   -1  one step the other way
 *    0  no change - the same state read twice, which a bouncing line does
 *    2  ILLEGAL: both lines changed between two interrupts. Either an edge
 *       was missed (too fast for this decoder) or a line is bouncing. The
 *       direction cannot be known, so the count is left alone and `errors`
 *       is incremented. Guessing here would quietly corrupt the position.
 *
 * The legal cycle is 00 -> 01 -> 11 -> 10 -> 00 one way and the reverse the
 * other, which is what the +1 and -1 entries below trace.
 */
static const int8_t qtab[16] = {
	/* prev 00 */  0, +1, -1,  2,
	/* prev 01 */ -1,  0,  2, +1,
	/* prev 11 */ +1,  2,  0, -1,
	/* prev 10 */  2, -1, +1,  0,
};

struct enc_hw {
	struct gpio_dt_spec a, b, i;
	const char *name;
};

#define ENC_SPEC(node) GPIO_DT_SPEC_GET(DT_NODELABEL(node), gpios)

static const struct enc_hw hw[PMC_ENC_COUNT] = {
	{ .a = ENC_SPEC(enc0a), .b = ENC_SPEC(enc0b), .i = ENC_SPEC(enc0i),
	  .name = "ENC0" },
	{ .a = ENC_SPEC(enc1a), .b = ENC_SPEC(enc1b), .i = ENC_SPEC(enc1i),
	  .name = "ENC1" },
};

/*
 * Written from interrupt context, read from threads. `count` is the only
 * value a reader can get torn - 32 bits on a 32-bit core is atomic for a
 * single load, so it is not, but it is marked anyway because the compiler is
 * the other party that can spoil this.
 */
struct enc_state {
	atomic_t count;
	atomic_t errors;
	atomic_t index_hits;
	int32_t  at_index;
	int32_t  per_index;
	int32_t  rate;
	int32_t  prev_count;
	int64_t  prev_stamp;
	uint8_t  prev_ab;
	bool     ready;

	/*
	 * ONE CALLBACK STRUCT PER PIN, AND THAT IS NOT TIDINESS.
	 *
	 * struct gpio_callback begins with a sys_snode_t: it IS the list node,
	 * not a thing that holds one. Registering a single struct on two ports
	 * - which is what A and B need here, because every channel straddles
	 * two ports - links the same node into two lists and corrupts both.
	 *
	 * That is exactly what the first version did, and the casualty was not
	 * the encoder. ENC0's A line is PJ8, so the shared node went into port
	 * J's callback list, where the WiFi driver keeps its host-wake
	 * callback on PJ5. The radio then failed every SDIO IOCTL with
	 * "failed to allocate a packet buffer" - twenty buffers held by
	 * transactions that never completed - and the encoders looked
	 * innocent because they still counted correctly.
	 */
	struct gpio_callback cb_a;
	struct gpio_callback cb_b;
	struct gpio_callback cb_i;
};

static struct enc_state st[PMC_ENC_COUNT];

static inline uint8_t read_ab(const struct enc_hw *h)
{
	return (uint8_t)((gpio_pin_get_dt(&h->a) ? 2 : 0) |
			 (gpio_pin_get_dt(&h->b) ? 1 : 0));
}

/*
 * One handler for both A and B. It does NOT look at which pin fired: the
 * decoder's whole job is to compare the pair's state against the last one it
 * saw, and asking which line woke it would be a second source of truth about
 * the same thing. Both lines are re-read here, so two edges arriving close
 * together collapse into one correct step rather than two guesses.
 */
static void a_isr(const struct device *port, struct gpio_callback *cb,
		  gpio_port_pins_t pins);
static void b_isr(const struct device *port, struct gpio_callback *cb,
		  gpio_port_pins_t pins);

static void ab_step(size_t n)
{
	struct enc_state *s = &st[n];
	const uint8_t now = read_ab(&hw[n]);
	const int8_t step = qtab[(s->prev_ab << 2) | now];

	s->prev_ab = now;

	if (step == 2) {
		atomic_inc(&s->errors);
		return;
	}
	if (step != 0) {
		atomic_add(&s->count, step);
	}
}

/*
 * Two entry points into one decoder. Neither cares which line woke it - both
 * are re-read in ab_step() - but they must be separate callback STRUCTS
 * because they are registered on different ports. See struct enc_state.
 */
static void a_isr(const struct device *port, struct gpio_callback *cb,
		  gpio_port_pins_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(pins);
	ab_step((size_t)(CONTAINER_OF(cb, struct enc_state, cb_a) - st));
}

static void b_isr(const struct device *port, struct gpio_callback *cb,
		  gpio_port_pins_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(pins);
	ab_step((size_t)(CONTAINER_OF(cb, struct enc_state, cb_b) - st));
}

static void i_isr(const struct device *port, struct gpio_callback *cb,
		  gpio_port_pins_t pins)
{
	struct enc_state *s = CONTAINER_OF(cb, struct enc_state, cb_i);
	const int32_t c = (int32_t)atomic_get(&s->count);

	ARG_UNUSED(port);
	ARG_UNUSED(pins);

	/*
	 * The distance between index pulses is the useful number: on a healthy
	 * channel it equals the encoder's line count times four, every
	 * revolution. A short one means counts were lost - which is the
	 * failure a software decoder has and a hardware one does not, so it is
	 * measured rather than assumed away.
	 */
	if (atomic_get(&s->index_hits) != 0) {
		s->per_index = c - s->at_index;
	}
	s->at_index = c;
	atomic_inc(&s->index_hits);
}

static int setup_chan(size_t n)
{
	const struct enc_hw *h = &hw[n];
	struct enc_state *s = &st[n];
	int rc;

	if (!gpio_is_ready_dt(&h->a) || !gpio_is_ready_dt(&h->b) ||
	    !gpio_is_ready_dt(&h->i)) {
		LOG_ERR("%s: a GPIO port is not ready", h->name);
		return -ENODEV;
	}

	rc = gpio_pin_configure_dt(&h->a, GPIO_INPUT);
	rc |= gpio_pin_configure_dt(&h->b, GPIO_INPUT);
	rc |= gpio_pin_configure_dt(&h->i, GPIO_INPUT);
	if (rc != 0) {
		LOG_ERR("%s: configure failed (%d)", h->name, rc);
		return -EIO;
	}

	/*
	 * BOTH EDGES on A and B - that is what makes it 4x. The index is a
	 * single pulse per revolution, so one edge is enough and taking both
	 * would count every revolution twice.
	 */
	rc = gpio_pin_interrupt_configure_dt(&h->a, GPIO_INT_EDGE_BOTH);
	rc |= gpio_pin_interrupt_configure_dt(&h->b, GPIO_INT_EDGE_BOTH);
	rc |= gpio_pin_interrupt_configure_dt(&h->i, GPIO_INT_EDGE_RISING);
	if (rc != 0) {
		LOG_ERR("%s: interrupt configure failed (%d) - see the EXTI "
			"note in enc.c", h->name, rc);
		return -EIO;
	}

	/*
	 * ONE STRUCT PER PIN, EACH ON ITS OWN PORT, EACH WITH A ONE-PIN MASK.
	 * Every channel here straddles two ports - ENC0 is gpioj and gpioh,
	 * ENC1 is gpioc and gpioi - and a gpio_callback is a list node, so it
	 * can belong to exactly one port's list. Sharing one across two is
	 * what broke the WiFi radio; see struct enc_state.
	 *
	 * A single-pin mask also matters on its own: a mask of
	 * BIT(8) | BIT(12) registered on port J would fire this handler for
	 * PJ12, a pin this file has nothing to do with.
	 */
	gpio_init_callback(&s->cb_a, a_isr, BIT(h->a.pin));
	rc = gpio_add_callback(h->a.port, &s->cb_a);
	if (rc != 0) {
		LOG_ERR("%s: A callback failed (%d)", h->name, rc);
		return -EIO;
	}

	gpio_init_callback(&s->cb_b, b_isr, BIT(h->b.pin));
	rc = gpio_add_callback(h->b.port, &s->cb_b);
	if (rc != 0) {
		LOG_ERR("%s: B callback failed (%d)", h->name, rc);
		return -EIO;
	}

	gpio_init_callback(&s->cb_i, i_isr, BIT(h->i.pin));
	rc = gpio_add_callback(h->i.port, &s->cb_i);
	if (rc != 0) {
		LOG_ERR("%s: index callback failed (%d)", h->name, rc);
		return -EIO;
	}

	s->prev_ab = read_ab(h);
	s->prev_stamp = k_uptime_get();
	s->ready = true;
	return 0;
}

int pmc_enc_init(void)
{
	/*
	 * HOW MANY, not whether - see CONFIG_PMC_ENCODER_CHANNELS. Arming both
	 * channels stops the WiFi radio answering any SDIO IOCTL, and this
	 * count is how that was narrowed down rather than argued about.
	 */
	const size_t want = MIN((size_t)CONFIG_PMC_ENCODER_CHANNELS,
				(size_t)PMC_ENC_COUNT);
	int bad = 0;

	for (size_t n = 0; n < want; n++) {
		if (setup_chan(n) != 0) {
			bad++;
		}
	}

	if (want == 0) {
		LOG_WRN("encoders not armed (CONFIG_PMC_ENCODER_CHANNELS=0)");
	} else if (bad == 0) {
		LOG_INF("encoders armed: %u channel(s), 4x, software decoded",
			(unsigned)want);
	}
	return bad == 0 ? 0 : -EIO;
}

void pmc_enc_sample(void)
{
	const int64_t now = k_uptime_get();

	for (size_t n = 0; n < PMC_ENC_COUNT; n++) {
		struct enc_state *s = &st[n];
		const int32_t c = (int32_t)atomic_get(&s->count);
		const int64_t dt = now - s->prev_stamp;

		if (!s->ready || dt <= 0) {
			continue;
		}
		/* Counts per second, from the difference over however long the
		   poll actually took - not over the interval it was asked for,
		   which a busy board does not honor. */
		s->rate = (int32_t)(((int64_t)(c - s->prev_count) * 1000) / dt);
		s->prev_count = c;
		s->prev_stamp = now;
	}
}

void pmc_enc_read(uint8_t n, struct pmc_enc_chan *out)
{
	if (out == NULL) {
		return;
	}
	memset(out, 0, sizeof(*out));
	if (n >= PMC_ENC_COUNT) {
		return;
	}

	const struct enc_hw *h = &hw[n];
	struct enc_state *s = &st[n];

	out->ready = s->ready;
	if (!s->ready) {
		return;
	}

	out->count = (int32_t)atomic_get(&s->count);
	out->errors = (uint32_t)atomic_get(&s->errors);
	out->index_hits = (uint32_t)atomic_get(&s->index_hits);
	out->at_index = s->at_index;
	out->per_index = s->per_index;
	out->rate = s->rate;
	out->a = gpio_pin_get_dt(&h->a) ? 1U : 0U;
	out->b = gpio_pin_get_dt(&h->b) ? 1U : 0U;
	out->i = gpio_pin_get_dt(&h->i) ? 1U : 0U;
}

bool pmc_enc_zero(uint8_t n, char *detail, size_t cap)
{
	if (n >= PMC_ENC_COUNT) {
		(void)snprintk(detail, cap, "refused: ENC%u is not a channel",
			       (unsigned)n);
		return false;
	}
	if (!st[n].ready) {
		(void)snprintk(detail, cap, "refused: ENC%u is not armed",
			       (unsigned)n);
		return false;
	}

	/*
	 * The error and index counters are NOT cleared. They are evidence
	 * about the wiring and the signal, and zeroing a position is a
	 * statement about where the machine is - not a claim that the channel
	 * has been healthy since boot.
	 */
	atomic_set(&st[n].count, 0);
	st[n].prev_count = 0;
	st[n].at_index = 0;
	(void)snprintk(detail, cap,
		       "ENC%u position zeroed; error and index counts kept",
		       (unsigned)n);
	return true;
}

/* ---------------------------------------------------------------- shell --- */

static int cmd_enc(const struct shell *sh, size_t argc, char **argv)
{
	if (argc == 3 && strcmp(argv[1], "zero") == 0) {
		char detail[72];
		const int n = atoi(argv[2]);

		if (pmc_enc_zero((uint8_t)n, detail, sizeof(detail))) {
			shell_print(sh, "%s", detail);
			return 0;
		}
		shell_error(sh, "%s", detail);
		return -EINVAL;
	}

	for (uint8_t n = 0; n < PMC_ENC_COUNT; n++) {
		struct pmc_enc_chan c;

		pmc_enc_read(n, &c);
		if (!c.ready) {
			shell_print(sh, "ENC%u  not armed", n);
			continue;
		}
		shell_print(sh, "ENC%u  count %-10d rate %-8d A%u B%u I%u",
			    n, c.count, c.rate, c.a, c.b, c.i);
		shell_print(sh, "      index %u hits, last at %d, %d counts "
				"between the last two",
			    c.index_hits, c.at_index, c.per_index);
		shell_print(sh, "      illegal transitions %u", c.errors);
	}

	shell_print(sh, "");
	shell_print(sh, "Decoded in software at 4x - no timer on this package");
	shell_print(sh, "can do it; see enc.h. Counts between index pulses");
	shell_print(sh, "should equal 4x the encoder's line count every");
	shell_print(sh, "revolution. A SHORT one means counts were lost, and");
	shell_print(sh, "that is the check - a clean error count is not one.");
	shell_print(sh, "");
	shell_print(sh, "  enc zero <0|1>   set the position to zero");
	return 0;
}

SHELL_CMD_ARG_REGISTER(enc, NULL,
		       "Encoder counts, rate, index and errors ('enc zero <n>').",
		       cmd_enc, 1, 2);
