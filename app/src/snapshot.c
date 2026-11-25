/*
 * One pass over the hardware. See snapshot.h for why this exists.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_stats.h>
#include <zephyr/net/hostname.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/timeutil.h>
#include <zephyr/sys/util.h>

#include <stm32_ll_system.h>

#include "clock.h"
#include "expander.h"
#include "health.h"
#include "io_write.h"
#include "rs485.h"
#include "snapshot.h"
#include "temp.h"

/* net.c is self-contained behind a SYS_INIT and has no header; this is its
 * only caller. Returns the WHD power save mode READ BACK from the radio:
 * 0 none, 1 PM1, 2 PM2, 99 never read. */
uint32_t pmc_net_wifi_ps_mode(void);

/* Same arrangement: the radio link as net.c last sampled it, with the age of
 * that sample. See struct pmc_link in snapshot.h for why it is cached. */
void pmc_net_link_read(struct pmc_link *out);

/* And the wire. present is 0 when &mac is disabled in the shield overlay, so
 * the panel distinguishes "no Ethernet in this build" from "no cable". */
void pmc_net_eth_read(struct pmc_eth *out);

/* And the addresses, for the panel's network window. REAL ADDRESSES - read
 * struct pmc_netinfo in snapshot.h before putting these anywhere. */
void pmc_net_info_read(struct pmc_netinfo *out);

/* Throughput the board measured on itself; see struct pmc_thru. */
void pmc_net_thru_read(struct pmc_thru *out);

#define AI_SWITCHES (LL_SYSCFG_ANALOG_SWITCH_PA1 | \
		     LL_SYSCFG_ANALOG_SWITCH_PC2 | \
		     LL_SYSCFG_ANALOG_SWITCH_PC3)

static const struct adc_dt_spec ai_spec[] = {
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0),
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 1),
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 2),
};
static const char *const ai_name[] = { "AI0", "AI1", "AI2" };
static const char *const ai_pad[]  = { "PC_3C", "PC_2C", "PA_1C" };

/*
 * Which SYSCFG analog switch belongs to which channel, and the order is NOT
 * the obvious one: AI0 is PC_3C, AI1 is PC_2C and AI2 is PA_1C, so the switch
 * list runs PC3, PC2, PA1. Getting this backwards would report AI0's switch
 * state against AI2's reading, and both would look plausible.
 */
static const uint32_t ai_switch[] = {
	LL_SYSCFG_ANALOG_SWITCH_PC3,
	LL_SYSCFG_ANALOG_SWITCH_PC2,
	LL_SYSCFG_ANALOG_SWITCH_PA1,
};

static int read_ai(size_t i, int16_t *raw, int32_t *mv)
{
	struct adc_sequence seq = { .buffer = raw, .buffer_size = sizeof(*raw) };

	if (!adc_is_ready_dt(&ai_spec[i])) {
		return -ENODEV;
	}
	if (adc_channel_setup_dt(&ai_spec[i]) != 0) {
		return -EIO;
	}
	if (adc_sequence_init_dt(&ai_spec[i], &seq) != 0) {
		return -EIO;
	}
	if (adc_read_dt(&ai_spec[i], &seq) != 0) {
		return -EIO;
	}
	*mv = *raw;
	(void)adc_raw_to_millivolts_dt(&ai_spec[i], mv);
	return 0;
}

static const uint8_t slot_id[] = {
#if PARTITION_EXISTS(slot0_partition)
	PARTITION_ID(slot0_partition),
#endif
#if PARTITION_EXISTS(slot1_partition)
	PARTITION_ID(slot1_partition),
#endif
};

/*
 * The carrier's RTC, found by compatible rather than by nodelabel so that this
 * compiles with the shield absent - which is how the application is built when
 * someone is bisecting a networking problem and does not want the carrier in
 * the way.
 */
#if DT_HAS_COMPAT_STATUS_OKAY(nxp_pcf8563)
static const struct device *const pmc_rtc =
	DEVICE_DT_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(nxp_pcf8563));
#else
static const struct device *const pmc_rtc;
#endif

static void read_clock(struct pmc_snapshot *s)
{
	struct rtc_time rt;
	int rc;

	s->clock_state = PMC_CLOCK_ABSENT;
	s->clock_epoch = 0;

	if (pmc_rtc == NULL || !device_is_ready(pmc_rtc)) {
		return;
	}

	rc = rtc_get_time(pmc_rtc, &rt);
	if (rc == -ENODATA) {
		/*
		 * The part is there and is telling us its oscillator stopped.
		 * That is a reading, not a failure, and it is the one that says
		 * the board has been unpowered longer than the supercapacitor
		 * holds.
		 */
		s->clock_state = PMC_CLOCK_LOST;
		return;
	}
	if (rc != 0) {
		return;
	}

	s->clock_state = PMC_CLOCK_HELD;
	s->clock_epoch = timeutil_timegm64(rtc_time_to_tm(&rt));
}

void pmc_snapshot_take(struct pmc_snapshot *s)
{
	const uint32_t pmcr = SYSCFG->PMCR & AI_SWITCHES;

	memset(s, 0, sizeof(*s));

	s->host = net_hostname_get();
	s->fw = CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION;
	s->uptime_s = (uint32_t)(k_uptime_get() / 1000);

	pmc_net_link_read(&s->link);
	pmc_net_eth_read(&s->eth);

	read_clock(s);
	pmc_clock_get(&s->drift);
	pmc_io_read(&s->io);
	pmc_rail_read(&s->rail);
	(void)pmc_do_read(s->do_out, ARRAY_SIZE(s->do_out));
	(void)pmc_pdio_commanded(s->pdio_cmd, ARRAY_SIZE(s->pdio_cmd));
	s->pulse_mask = pmc_io_pulse_mask();
	pmc_rs485_activity(&s->rs485_tx_age, &s->rs485_rx_age);

	for (uint8_t n = 0; n < PMC_ENC_COUNT; n++) {
		pmc_enc_read(n, &s->enc[n]);
	}
	pmc_can_state_read(&s->can);
	s->n_can_log = pmc_can_log_read(s->can_log, ARRAY_SIZE(s->can_log));
	pmc_canopen_state_read(&s->co);
	pmc_net_info_read(&s->net);
	pmc_net_thru_read(&s->thru);

	s->n_ai = ARRAY_SIZE(ai_spec);
	for (size_t i = 0; i < s->n_ai; i++) {
		int16_t raw = 0;
		int32_t mv = 0;
		const int rc = read_ai(i, &raw, &mv);

		s->ai[i].name = ai_name[i];
		s->ai[i].pad = ai_pad[i];
		s->ai[i].ok = (rc == 0);
		s->ai[i].raw = (rc == 0) ? raw : -1;
		s->ai[i].mv = (rc == 0) ? mv : -1;
		/*
		 * A 1 in the PMCR bit means the switch is OPEN. The reset value
		 * is 0, i.e. CLOSED, which ties PA1 to the Ethernet RMII clock
		 * and PC2/PC3 to SPI2 - in both directions. A reading taken
		 * with the switch closed is not a measurement of anything on
		 * the terminal block, which is why this travels beside the
		 * value rather than being checked once at boot.
		 */
		s->ai[i].open = (pmcr & ai_switch[i]) != 0U;
	}

	s->n_slot = ARRAY_SIZE(slot_id);
	for (size_t i = 0; i < s->n_slot; i++) {
		struct mcuboot_img_header hdr;

		s->slot[i].slot = (uint8_t)i;
		s->slot[i].active = (i == 0);

		if (boot_read_bank_header(slot_id[i], &hdr, sizeof(hdr)) == 0) {
			s->slot[i].present = true;
			/*
			 * THE BUILD NUMBER IS PART OF THE NAME, not a detail.
			 * Since 2026-11-19 maj.min.rev is the release and
			 * +build is the flash counter (docs/CONFIG.md), so
			 * printing only the first three makes every image of
			 * one release identical here - which is the exact
			 * thing a version on a panel must not do. It is read
			 * from the header beside the others; leaving it out
			 * was never cheaper, only shorter.
			 */
			snprintk(s->slot[i].version, sizeof(s->slot[i].version),
				 "%u.%u.%u+%u", hdr.h.v1.sem_ver.major,
				 hdr.h.v1.sem_ver.minor,
				 hdr.h.v1.sem_ver.revision,
				 hdr.h.v1.sem_ver.build_num);
		} else {
			strncpy(s->slot[i].version, "-",
				sizeof(s->slot[i].version) - 1);
		}

		/*
		 * boot_is_img_confirmed() reports on the RUNNING image, so it
		 * is only meaningful for slot 0. Printing it against slot 1
		 * would claim the staged image is confirmed, which is the one
		 * thing it cannot be.
		 */
		s->slot[i].confirmed = (i == 0) && boot_is_img_confirmed();
	}
}

/* ------------------------------------------------- the analog history --- */

/*
 * A ring of past AI readings, and the polyline the panel draws from it.
 *
 * SAMPLED ON ITS OWN TIMER, not when a page renders. A trend built from page
 * loads would be a graph of when somebody was looking, with gaps shaped like
 * lunch - and two clients would corrupt each other's spacing. This is the one
 * thing on the panel that cannot be derived at render time.
 *
 * Y IS FIXED AT 0..10 V, the board's own input range, rather than autoscaled.
 * An autoscaled trace makes 3 mV of noise look identical to a 10 V ramp, and
 * the whole point of a trend here is to see a signal against its range.
 *
 * mV is stored, not raw counts, because raw is meaningless once the mode
 * switches. INT16_MIN marks a slot whose read failed, so a gap renders as a
 * gap rather than as a plunge to zero.
 */
#define AI_HIST_N    120U
#define AI_HIST_MS   10000U
#define AI_HIST_GAP  INT16_MIN
#define AI_FULLSCALE 10000       /* mV at the top of the plot */
#define AI_PLOT_W    318
#define AI_PLOT_H    88

static int16_t  s_hist[PMC_SNAP_AI][AI_HIST_N];
static uint16_t s_hist_n;
static uint16_t s_hist_head;
static struct k_work_delayable s_hist_work;

static void ai_hist_sample(struct k_work *w)
{
	ARG_UNUSED(w);

	for (size_t i = 0; i < PMC_SNAP_AI && i < ARRAY_SIZE(ai_spec); i++) {
		int16_t raw = 0;
		int32_t mv = 0;

		s_hist[i][s_hist_head] = (read_ai(i, &raw, &mv) == 0)
						 ? (int16_t)mv
						 : AI_HIST_GAP;
	}

	s_hist_head = (uint16_t)((s_hist_head + 1U) % AI_HIST_N);
	if (s_hist_n < AI_HIST_N) {
		s_hist_n++;
	}

	k_work_reschedule(&s_hist_work, K_MSEC(AI_HIST_MS));
}

static int ai_hist_init(void)
{
	for (size_t c = 0; c < PMC_SNAP_AI; c++) {
		for (size_t j = 0; j < AI_HIST_N; j++) {
			s_hist[c][j] = AI_HIST_GAP;
		}
	}

	k_work_init_delayable(&s_hist_work, ai_hist_sample);
	k_work_reschedule(&s_hist_work, K_MSEC(2000));
	return 0;
}

SYS_INIT(ai_hist_init, APPLICATION, 50);

size_t pmc_snapshot_ai_points(size_t ch, char *out, size_t cap)
{
	size_t n = 0;

	if (out == NULL || cap == 0) {
		return 0;
	}
	out[0] = '\0';
	if (ch >= PMC_SNAP_AI || s_hist_n < 2U) {
		return 0;
	}

	/*
	 * Oldest first. The ring is only full once s_hist_n saturates, so
	 * before that the oldest sample is index 0, not head.
	 */
	const uint16_t first = (s_hist_n == AI_HIST_N) ? s_hist_head : 0U;

	for (uint16_t k = 0; k < s_hist_n; k++) {
		const uint16_t idx = (uint16_t)((first + k) % AI_HIST_N);
		const int16_t mv = s_hist[ch][idx];
		int32_t y;

		if (mv == AI_HIST_GAP) {
			continue;   /* a gap breaks the line, honestly */
		}

		y = AI_PLOT_H - ((int32_t)mv * AI_PLOT_H) / AI_FULLSCALE;
		y = CLAMP(y, 0, AI_PLOT_H);

		n += snprintk(out + n, (n < cap) ? cap - n : 0, "%d,%d ",
			      (int)(((int32_t)k * AI_PLOT_W) /
				    (int32_t)(s_hist_n - 1U)),
			      (int)y);
		if (n >= cap) {
			return cap - 1;
		}
	}

	return n;
}

uint32_t pmc_snapshot_ai_span_s(void)
{
	return (s_hist_n < 2U) ? 0U
			       : ((uint32_t)(s_hist_n - 1U) * AI_HIST_MS) / 1000U;
}

int pmc_snapshot_reading(const struct pmc_snapshot *s, const char *chan,
			 char *out, size_t cap)
{
	if (chan == NULL || chan[0] == '\0') {
		(void)snprintk(out, cap, "not a channel");
		return -1;
	}

	for (size_t i = 0; i < s->n_ai; i++) {
		if (strcmp(chan, s->ai[i].name) != 0) {
			continue;
		}
		if (!s->ai[i].ok) {
			(void)snprintk(out, cap, "ADC read failed");
			return -1;
		}
		(void)snprintk(out, cap, "%d raw, %d mV%s", s->ai[i].raw,
			       s->ai[i].mv,
			       s->ai[i].open ? "" :
			       " - SWITCH CLOSED, not a terminal reading");
		return -1; /* an analog level is not a logic level */
	}

	/*
	 * DIN and PDIO now come off the expanders. The level is real; the
	 * MAPPING is not yet - every channel map here is transcribed from the
	 * mbed library and has not been confirmed at the connector, so the
	 * text says which bit was read and the panel keeps the dashed border
	 * until tools/terminals.csv says `measured`.
	 */
	if (strncmp(chan, "DIN", 3) == 0) {
		const int n = atoi(chan + 3);

		if (n >= 0 && n < PMC_DIN_COUNT) {
			if (!s->io.din_ok) {
				(void)snprintk(out, cap,
					       "DIN expander did not answer (%d)",
					       s->io.din_err);
				return -1;
			}
			(void)snprintk(out, cap, "%s (map unverified)",
				       s->io.din[n] ? "HIGH" : "LOW");
			return s->io.din[n];
		}
	}

	if (strncmp(chan, "PDIO", 4) == 0) {
		const int n = atoi(chan + 4);

		if (n >= 0 && n < PMC_PDIO_COUNT) {
			if (!s->io.pdio_ok) {
				(void)snprintk(out, cap,
					       "PDIO expander did not answer (%d)",
					       s->io.pdio_err);
				return -1;
			}
			(void)snprintk(out, cap, "%s (map unverified)",
				       s->io.pdio[n] ? "HIGH" : "LOW");
			return s->io.pdio[n];
		}
	}

	/*
	 * DO IS KNOWN, NOT GUESSED. These are H7 GPIOs, and the state of a pin
	 * this firmware configured and drove is held in the port's own
	 * registers - read back here rather than from a shadow. The board boots
	 * every DO de-energized in pmc_io_write_init(), so the value is defined
	 * from reset onward and there is no "unknown" to report.
	 *
	 * The panel lights the channel LED from this, with no caveat attached.
	 * Whether 24 V reaches the terminal is a property of the wiring, not a
	 * claim the controller is making.
	 */
	if (strncmp(chan, "DO", 2) == 0 && chan[2] >= '0' && chan[2] <= '9') {
		const int n = atoi(chan + 2);

		if (n >= 0 && n < (int)ARRAY_SIZE(s->do_out)) {
			if (s->do_out[n] < 0) {
				(void)snprintk(out, cap, "DO%d port not ready",
					       n);
				return -1;
			}
			(void)snprintk(out, cap, "%s",
				       s->do_out[n] ? "ON" : "OFF");
			return s->do_out[n];
		}
	}

	/* TEMP, ENC, RS485 and CAN have no reader. Saying so is the point. */
	(void)snprintk(out, cap, "no reader in this image yet");
	return -1;
}

/*
 * THE LAMP LEVEL, WHICH IS NOT THE READING FOR AN OUTPUT.
 *
 * pmc_snapshot_reading() answers "what does this channel read", and for PDIO
 * that is the terminal through the read map. A lamp over an OUTPUT must
 * answer a different question - "what did you tell it to do" - or commanding
 * a channel with its connector's 24V IN unfed, or with a mis-transcribed
 * map, lights nothing and looks like a dead control.
 *
 * So: DIN is an input and its lamp is its reading; DO and PDIO are outputs
 * and their lamps are their commanded level. -1 where there is no answer,
 * which the panel renders as "leave it alone" rather than as off.
 */
int pmc_snapshot_lamp(const struct pmc_snapshot *s, const char *chan)
{
	if (chan == NULL || chan[0] == '\0') {
		return -1;
	}

	if (strncmp(chan, "PDIO", 4) == 0) {
		const int n = atoi(chan + 4);

		if (n >= 0 && n < (int)ARRAY_SIZE(s->pdio_cmd)) {
			return s->pdio_cmd[n];
		}
		return -1;
	}

	if (strncmp(chan, "DO", 2) == 0 && chan[2] >= '0' && chan[2] <= '9') {
		const int n = atoi(chan + 2);

		if (n >= 0 && n < (int)ARRAY_SIZE(s->do_out)) {
			return s->do_out[n];
		}
		return -1;
	}

	if (strncmp(chan, "DIN", 3) == 0) {
		const int n = atoi(chan + 3);

		if (s->io.din_ok && n >= 0 && n < PMC_DIN_COUNT) {
			return s->io.din[n];
		}
		return -1;
	}

	return -1;
}

size_t pmc_snapshot_json(const struct pmc_snapshot *s, char *out, size_t cap)
{
	size_t n = 0;

	n += snprintk(out + n, cap - n,
		      "{\"host\":\"%s\",\"fw\":\"%s\",\"uptime\":%u,",
		      s->host, s->fw, s->uptime_s);

	/* epoch is 0 unless state is "held", so a reader that only looks at the
	 * number cannot mistake a lost clock for 1970. */
	n += snprintk(out + n, cap - n,
		      "\"clock\":{\"state\":\"%s\",\"epoch\":%lld,",
		      s->clock_state == PMC_CLOCK_HELD ? "held"
			: s->clock_state == PMC_CLOCK_LOST ? "lost" : "absent",
		      (long long)s->clock_epoch);

	/* ppm is in tenths and means nothing without its span, so both travel
	 * together and "measured" says whether either means anything yet. */
	n += snprintk(out + n, cap - n,
		      "\"measured\":%s,\"ppmTenths\":%d,\"spanS\":%u,"
		      "\"setEpoch\":%lld,\"syncs\":%u,\"ntpFailures\":%u},",
		      s->drift.valid ? "true" : "false", s->drift.ppm_tenths,
		      s->drift.span_s, (long long)s->drift.set_epoch,
		      s->drift.syncs, s->drift.failures);

	n += snprintk(out + n, cap - n, "\"health\":[");
	for (size_t i = 0; i < pmc_health_check_count(); i++) {
		const char *detail = "";
		const bool ok = pmc_health_check(i, &detail);

		n += snprintk(out + n, cap - n,
			      "%s{\"name\":\"%s\",\"ok\":%s,\"detail\":\"%s\"}",
			      i ? "," : "", pmc_health_check_name(i),
			      ok ? "true" : "false", detail);
	}
	n += snprintk(out + n, cap - n, "],");

	n += snprintk(out + n, cap - n,
		      "\"io\":{\"dinOk\":%s,\"pdioOk\":%s,\"din\":[",
		      s->io.din_ok ? "true" : "false",
		      s->io.pdio_ok ? "true" : "false");
	for (size_t i = 0; i < PMC_DIN_COUNT; i++) {
		n += snprintk(out + n, cap - n, "%s%d", i ? "," : "", s->io.din[i]);
	}
	n += snprintk(out + n, cap - n, "],\"pdio\":[");
	for (size_t i = 0; i < PMC_PDIO_COUNT; i++) {
		n += snprintk(out + n, cap - n, "%s%d", i ? "," : "", s->io.pdio[i]);
	}

	/*
	 * THE DO LOGIC LINES, WHICH THIS DOCUMENT USED TO OMIT ENTIRELY.
	 *
	 * The board graphic lights a channel LED from pmc_snapshot_reading(),
	 * which is evaluated when the PAGE renders - so after a command the
	 * lamp kept whatever it was drawn with and nothing refreshed it. The
	 * panel polls this document; without `do` here the one family with a
	 * definite answer was the one family it could not show.
	 *
	 * -1 means the port was not ready, and that is kept distinct from 0
	 * rather than flattened: see the comment on do_out in snapshot.h.
	 *
	 * Named `doOut` rather than `do`, which is a reserved word in the one
	 * language guaranteed to read this document.
	 */
	n += snprintk(out + n, cap - n, "],\"doOut\":[");
	for (size_t i = 0; i < ARRAY_SIZE(s->do_out); i++) {
		n += snprintk(out + n, cap - n, "%s%d", i ? "," : "",
			      (int)s->do_out[i]);
	}

	/* The commanded PDIO level, beside the read-back and never instead of
	   it. The lamp follows this one; the table and the tooltip keep
	   reporting what the terminal actually reads, which is the only way
	   to see the two disagree. */
	n += snprintk(out + n, cap - n, "],\"pdioCmd\":[");
	for (size_t i = 0; i < ARRAY_SIZE(s->pdio_cmd); i++) {
		n += snprintk(out + n, cap - n, "%s%d", i ? "," : "",
			      (int)s->pdio_cmd[i]);
	}
	n += snprintk(out + n, cap - n, "],\"doPulse\":%u},",
		      (unsigned)s->pulse_mask);

	/*
	 * RS485 ACTIVITY AS AN AGE, so the lamp can blink rather than sit.
	 *
	 * Rendered server-side the bus lamps were decided once, at page load,
	 * and then held that verdict forever: a page loaded during a quiet
	 * moment showed a dead bus through an hour of traffic, and one loaded
	 * mid-burst blinked for an hour after the last byte. Published as the
	 * age in ms of the last byte each way - NOT as a boolean - because the
	 * window that counts as "active" is a presentation choice and belongs
	 * on the side that is doing the presenting. -1 is never.
	 */
	n += snprintk(out + n, cap - n,
		      "\"rs485\":{\"txAge\":%d,\"rxAge\":%d},",
		      (int)s->rs485_tx_age, (int)s->rs485_rx_age);

	/*
	 * The encoders. `per` is the count between the last two index pulses
	 * and is the only honest check that nothing was missed - see enc.h.
	 */
	n += snprintk(out + n, cap - n, "\"enc\":[");
	for (size_t i = 0; i < PMC_ENC_COUNT; i++) {
		const struct pmc_enc_chan *e = &s->enc[i];

		n += snprintk(out + n, cap - n,
			      "%s{\"ok\":%s,\"count\":%d,\"rate\":%d,"
			      "\"a\":%u,\"b\":%u,\"z\":%u,\"idx\":%u,"
			      "\"per\":%d,\"err\":%u}",
			      i ? "," : "", e->ready ? "true" : "false",
			      e->count, e->rate, e->a, e->b, e->z,
			      e->index_hits, e->per_index, e->errors);
	}
	n += snprintk(out + n, cap - n, "],");

	/*
	 * CAN. `txNoAck` is separate from `txFail` on purpose: alone on a bus
	 * nothing acknowledges a frame, and that is the bus answering rather
	 * than a broken controller.
	 */
	n += snprintk(out + n, cap - n,
		      "\"can\":{\"present\":%s,\"started\":%s,\"loopback\":%s,"
		      "\"stb\":%u,\"bitrate\":%u,\"state\":%d,"
		      "\"txErr\":%u,\"rxErr\":%u,\"txOk\":%u,\"txNoAck\":%u,"
		      "\"txFail\":%u,\"txQueued\":%u,\"txReject\":%u,"
		      "\"txPin\":%u,\"rx\":%u,\"log\":[",
		      s->can.present ? "true" : "false",
		      s->can.started ? "true" : "false",
		      s->can.loopback ? "true" : "false",
		      (unsigned)s->can.stb_level, (unsigned)s->can.bitrate,
		      s->can.bus_state,
		      (unsigned)s->can.tx_err_cnt, (unsigned)s->can.rx_err_cnt,
		      (unsigned)s->can.tx_ok, (unsigned)s->can.tx_no_ack,
		      (unsigned)s->can.tx_fail, (unsigned)s->can.tx_queued,
		      (unsigned)s->can.tx_reject,
		      (unsigned)s->can.tx_pin_toggling,
		      (unsigned)s->can.rx_frames);

	for (size_t i = 0; i < s->n_can_log; i++) {
		const struct pmc_can_frame *f = &s->can_log[i];
		char hex[2 * 8 + 1] = { 0 };
		size_t p = 0;

		for (uint8_t b = 0; b < f->dlc && b < 8U; b++) {
			p += (size_t)snprintk(hex + p, sizeof(hex) - p, "%02x",
					      f->data[b]);
		}
		n += snprintk(out + n, cap - n,
			      "%s{\"id\":%u,\"ext\":%u,\"rtr\":%u,\"dlc\":%u,"
			      "\"d\":\"%s\",\"t\":%lld}",
			      i ? "," : "", (unsigned)f->id, f->ext, f->rtr,
			      f->dlc, hex, f->stamp);
	}
	n += snprintk(out + n, cap - n, "]},");

	/*
	 * The CANopen node, beside the controller it speaks through rather
	 * than in a section of its own: a node id without the bus state next
	 * to it is half an answer, because a node in pre-operational on a
	 * controller nobody acknowledges is not a node anything can reach.
	 */
	n += snprintk(out + n, cap - n,
		      "\"canopen\":{\"on\":%s,\"node\":%u,\"state\":%u,"
		      "\"hbMs\":%u,\"boots\":%u,\"hb\":%u,\"nmt\":%u,"
		      "\"sdo\":%u,\"sdoOk\":%u,\"sdoAbort\":%u,"
		      "\"drop\":%u,\"rev\":%u,\"serial\":%u},",
		      s->co.enabled ? "true" : "false",
		      (unsigned)s->co.node, (unsigned)s->co.state,
		      (unsigned)s->co.hb_ms, (unsigned)s->co.boots,
		      (unsigned)s->co.hb_sent, (unsigned)s->co.nmt_rx,
		      (unsigned)s->co.sdo_rx, (unsigned)s->co.sdo_ok,
		      (unsigned)s->co.sdo_abort, (unsigned)s->co.dropped,
		      (unsigned)s->co.revision, (unsigned)s->co.serial);

	/*
	 * THE RADIO'S OWN DROP COUNTER, and the reason it is here.
	 *
	 * Three explanations for a stalled OTA have been proposed on this
	 * bench - processor load, WiFi power save, and the panel's own
	 * polling - and the board refuted or complicated all three. Each was
	 * reasoned rather than measured. This is the measurement.
	 *
	 * The AIROC driver increments errors.rx on exactly one path: it could
	 * not get a net buffer for an arriving frame, logs "Failed to get net
	 * buffer", and drops it. That happens in interrupt context, before any
	 * thread priority can express a preference - which is precisely why a
	 * priority-0 SMP thread does not help. If rxDrops climbs while an
	 * upload stalls, the receive pool is the cause and the fix is pool
	 * sizing or traffic classes. If it stays at zero while an upload
	 * fails, the RECEIVE pool is cleared - and nothing beyond it.
	 *
	 * Read txErrs beside it. It is a different mechanism in the other
	 * direction, and reading rxDrops alone is how it was missed once
	 * already. The driver increments errors.tx when WHD refuses the
	 * buffer, and WHD refuses it when the send queue for that 802.11
	 * access category already holds 64 packets. Every frame this board
	 * sends carries DSCP 0, so the small SMP responses and the large
	 * HTTP responses share one such queue. A dropped SMP response in a
	 * lockstep protocol is a client timeout - a stall, not a slowdown.
	 * The trace is in docs/MEASURED.md.
	 *
	 * Either answer is worth more than another round of reasoning.
	 */
	{
		struct net_stats_wifi ws = { 0 };
		struct net_if *wifi = net_if_get_first_wifi();
		int rc = -ENODEV;

		if (wifi != NULL) {
			rc = net_mgmt(NET_REQUEST_STATS_GET_WIFI, wifi, &ws,
				      sizeof(ws));
		}
		n += snprintk(out + n, cap - n,
			      "\"wifi\":{\"ok\":%s,\"rxPkts\":%u,"
			      "\"rxDrops\":%u,\"txPkts\":%u,"
			      "\"txErrs\":%u},",
			      (rc == 0) ? "true" : "false",
			      (unsigned)ws.pkts.rx, (unsigned)ws.errors.rx,
			      (unsigned)ws.pkts.tx, (unsigned)ws.errors.tx);
	}

	/*
	 * The WiFi power save mode, read back from the radio rather than
	 * inferred from the return code of the call that set it. 2 means PM2
	 * is still on, and a large inbound transfer will stall; 0 means it is
	 * genuinely off. This board has no console, so this is the only place
	 * the answer can be seen - which is the entire point of publishing it.
	 */
	n += snprintk(out + n, cap - n, "\"wifiPsMode\":%u,",
		      (unsigned)pmc_net_wifi_ps_mode());

	/*
	 * THE LINK. rssi is the number that was missing while five software
	 * theories were argued about a stalled OTA; rateTenths is the one that
	 * says whether the radio has fallen back to a slow modulation, which is
	 * what a marginal link does before it starts losing frames outright.
	 * ageMs of -1 means no sample has ever succeeded - distinct from a
	 * reading of 0 dBm, which would be a very strong signal indeed.
	 */
	n += snprintk(out + n, cap - n,
		      "\"link\":{\"ageMs\":%d,\"rc\":%d,\"rssi\":%d,"
		      "\"chan\":%u,\"rateTenths\":%u,\"mode\":%u,"
		      "\"state\":%u},",
		      (int)s->link.age_ms, (int)s->link.rc, (int)s->link.rssi,
		      (unsigned)s->link.chan, (unsigned)s->link.rate_tenths,
		      (unsigned)s->link.mode, (unsigned)s->link.state);

	/*
	 * THE WIRE. carrier and link are both published because they come from
	 * different places - the MAC driver and the PHY over MDIO - and
	 * disagree precisely when something is wrong. hasAddr is a boolean and
	 * not the address: this page gets screenshotted into a public
	 * repository.
	 */
	n += snprintk(out + n, cap - n,
		      "\"eth\":{\"present\":%s,\"ageMs\":%d,\"rc\":%d,"
		      "\"carrier\":%s,\"link\":%s,\"fullDuplex\":%s,"
		      "\"speedMbit\":%u,\"forced\":%s,"
		      "\"hasAddr\":%s,\"initRes\":%d",
		      s->eth.present ? "true" : "false",
		      (int)s->eth.age_ms, (int)s->eth.rc,
		      s->eth.carrier ? "true" : "false",
		      s->eth.link_up ? "true" : "false",
		      s->eth.full_duplex ? "true" : "false",
		      (unsigned)s->eth.speed_mbit,
		      s->eth.forced ? "true" : "false",
		      s->eth.has_addr ? "true" : "false",
		      (int)s->eth.init_res);

	/* Only when the MAC failed init, which is the only time it is read. */
	if (s->eth.probed) {
		n += snprintk(out + n, cap - n,
			      ",\"gates\":%u,\"epis\":%u,\"sw\":%u,\"phyRst\":%u,"
			      "\"swr\":%u,\"phyAddr\":%d,\"phyId\":\"%04x:%04x\"",
			      (unsigned)s->eth.gates, (unsigned)s->eth.epis,
			      (unsigned)s->eth.pmcr_sw,
			      (unsigned)s->eth.phy_rst, (unsigned)s->eth.swr,
			      (int)s->eth.phy_addr, (unsigned)s->eth.phy_id1,
			      (unsigned)s->eth.phy_id2);
	}

	n += snprintk(out + n, cap - n, "},");

	/*
	 * TEMPERATURE. The mode is reported beside the readings because a
	 * number without it is meaningless - the same channel reads a
	 * different temperature through the other converter, and J and K come
	 * from the same part. "sweeps" is here so a stalled rotation is
	 * visible: three unchanging numbers and a frozen counter is a dead
	 * thread, while three unchanging numbers and a rising counter is a
	 * room that is not getting warmer.
	 */
	n += snprintk(out + n, cap - n,
		      "\"temp\":{\"mode\":\"%s\",\"wires\":%u,\"ready\":%s,"
		      "\"sweeps\":%u,\"faults\":%u,\"modeSets\":%u,\"chan\":[",
		      pmc_temp_mode_name(pmc_temp_mode_get()),
		      (unsigned)pmc_temp_wires_get(),
		      pmc_temp_ready() ? "true" : "false",
		      (unsigned)pmc_temp_sweeps(), (unsigned)pmc_temp_faults(),
		      (unsigned)pmc_temp_mode_sets());
	for (size_t i = 0; i < PMC_TEMP_COUNT; i++) {
		struct pmc_temp_chan c;
		const char *st = "unread";

		(void)pmc_temp_get((uint8_t)i, &c);
		switch (c.state) {
		case PMC_T_OK:    st = "ok";    break;
		case PMC_T_OPEN:  st = "open";  break;
		case PMC_T_FAULT: st = "fault"; break;
		case PMC_T_ERR:   st = "error"; break;
		default:          st = "unread"; break;
		}
		n += snprintk(out + n, cap - n,
			      "%s{\"state\":\"%s\",\"mC\":%d,\"rawMC\":%d,"
			      "\"coldMC\":%d,\"word\":\"%08x\","
			      "\"rtdFault\":%u,\"cause\":\"%s\"}",
			      i ? "," : "", st, (int)c.milli_c,
			      (int)c.raw_milli_c, (int)c.cold_milli_c,
			      (unsigned)c.raw_word, (unsigned)c.rtd_fault,
			      pmc_temp_rtd_fault_str(c.rtd_fault)
				      ? pmc_temp_rtd_fault_str(c.rtd_fault) : "");
	}
	n += snprintk(out + n, cap - n, "]},");

	/* "inferred" is in the name on purpose: nothing here measures a rail. */
	n += snprintk(out + n, cap - n,
		      "\"rail24Inferred\":{\"sensed\":%s,\"present\":%s,"
		      "\"encoderWired\":%s,\"highs\":%u},",
		      s->rail.sensed ? "true" : "false",
		      s->rail.present ? "true" : "false",
		      s->rail.wired ? "true" : "false", s->rail.highs);

	/* The swap state. With no console this is the only place a REVERT or a
	 * FAIL is visible, and a FAIL disables every future update until the
	 * trailer is cleared. */
	n += snprintk(out + n, cap - n,
		      "\"boot\":{\"swapType\":\"%s\",\"confirmed\":%s},",
		      pmc_swap_type_name(),
		      pmc_img_confirmed() ? "true" : "false");

	n += snprintk(out + n, cap - n, "\"slots\":[");
	for (size_t i = 0; i < s->n_slot; i++) {
		n += snprintk(out + n, cap - n,
			      "%s{\"slot\":%u,\"version\":\"%s\",\"active\":%s,"
			      "\"confirmed\":%s}",
			      i ? "," : "", s->slot[i].slot, s->slot[i].version,
			      s->slot[i].active ? "true" : "false",
			      s->slot[i].confirmed ? "true" : "false");
	}
	n += snprintk(out + n, cap - n, "],");

	n += snprintk(out + n, cap - n, "\"ai\":[");
	for (size_t i = 0; i < s->n_ai; i++) {
		n += snprintk(out + n, cap - n,
			      "%s{\"name\":\"%s\",\"pad\":\"%s\",\"raw\":%d,"
			      "\"mv\":%d,\"open\":%s}",
			      i ? "," : "", s->ai[i].name, s->ai[i].pad,
			      s->ai[i].raw, s->ai[i].mv,
			      s->ai[i].open ? "true" : "false");
	}
	n += snprintk(out + n, cap - n, "]}");

	return n < cap ? n : cap - 1;
}
