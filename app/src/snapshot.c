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
#include <zephyr/net/hostname.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/timeutil.h>
#include <zephyr/sys/util.h>

#include <stm32_ll_system.h>

#include "clock.h"
#include "expander.h"
#include "health.h"
#include "snapshot.h"

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

	read_clock(s);
	pmc_clock_get(&s->drift);
	pmc_io_read(&s->io);
	pmc_rail_read(&s->rail);

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
			snprintk(s->slot[i].version, sizeof(s->slot[i].version),
				 "%u.%u.%u", hdr.h.v1.sem_ver.major,
				 hdr.h.v1.sem_ver.minor,
				 hdr.h.v1.sem_ver.revision);
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
	 * DO is H7 GPIOs behind a shared latch rather than expander bits, so it
	 * arrives with the write path. TEMP, ENC, RS485 and CAN have no reader
	 * either. Saying so is the point - see the header.
	 */
	(void)snprintk(out, cap, "no reader in this image yet");
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
