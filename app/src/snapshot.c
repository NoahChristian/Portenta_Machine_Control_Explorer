/*
 * One pass over the hardware. See snapshot.h for why this exists.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 */

#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/net/hostname.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/util.h>

#include <stm32_ll_system.h>

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

void pmc_snapshot_take(struct pmc_snapshot *s)
{
	const uint32_t pmcr = SYSCFG->PMCR & AI_SWITCHES;

	memset(s, 0, sizeof(*s));

	s->host = net_hostname_get();
	s->fw = CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION;
	s->uptime_s = (uint32_t)(k_uptime_get() / 1000);

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
	 * Everything else. DIN, DO and PDIO live on the two TCA6424A expanders
	 * and nothing in this image reads them yet; TEMP, ENC, RS485 and CAN
	 * have no reader either. Saying so is the point - see the header.
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
