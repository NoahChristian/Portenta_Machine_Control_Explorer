/*
 * Analog out. Why nothing here can say "verified" is in ao.h.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include "ao.h"

LOG_MODULE_REGISTER(pmc_ao, LOG_LEVEL_INF);

static const struct pwm_dt_spec ao_pwm[] = {
	PWM_DT_SPEC_GET(DT_NODELABEL(ao0)),
	PWM_DT_SPEC_GET(DT_NODELABEL(ao1)),
	PWM_DT_SPEC_GET(DT_NODELABEL(ao3)),
};

/* Channel number -> index into ao_pwm, or -1. AO2 is the hole. */
static const int8_t ao_slot[PMC_AO_COUNT] = { 0, 1, -1, 2 };

static int32_t s_mv[PMC_AO_COUNT] = { -1, -1, -1, -1 };
static bool s_ready;

bool pmc_ao_available(int n)
{
	return n >= 0 && n < PMC_AO_COUNT && ao_slot[n] >= 0;
}

int32_t pmc_ao_commanded(int n)
{
	if (n < 0 || n >= PMC_AO_COUNT) {
		return -1;
	}
	return s_mv[n];
}

int pmc_ao_init(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(ao_pwm); i++) {
		if (!pwm_is_ready_dt(&ao_pwm[i])) {
			LOG_ERR("AO pwm %u not ready", (unsigned)i);
			return -ENODEV;
		}
	}

	/*
	 * Every channel to zero before anything else can set one. An analog
	 * output that comes up at whatever the timer happened to hold is a
	 * 0-10 V signal nobody asked for, and after an OTA revert the previous
	 * image's setpoints are exactly what would otherwise persist.
	 */
	for (int n = 0; n < PMC_AO_COUNT; n++) {
		if (!pmc_ao_available(n)) {
			continue;
		}
		(void)pwm_set_pulse_dt(&ao_pwm[ao_slot[n]], 0);
		s_mv[n] = 0;
	}

	s_ready = true;
	LOG_INF("AO ready: AO0, AO1, AO3 (AO2 is HRTIM, unavailable)");
	return 0;
}

enum pmc_write_result pmc_ao_set(const char *chan, uint32_t mv,
				 char *detail, size_t cap)
{
	int n;
	uint32_t pulse;
	int rc;

	if (!s_ready) {
		(void)snprintk(detail, cap, "AO not initialized");
		return PMC_W_ERROR;
	}
	if (chan == NULL || strncmp(chan, "AO", 2) != 0) {
		(void)snprintk(detail, cap, "%s is not an analog output",
			       chan ? chan : "(none)");
		return PMC_W_REFUSED;
	}

	n = atoi(chan + 2);
	if (n < 0 || n >= PMC_AO_COUNT) {
		(void)snprintk(detail, cap, "%s is not a channel", chan);
		return PMC_W_REFUSED;
	}
	if (!pmc_ao_available(n)) {
		(void)snprintk(detail, cap,
			       "AO%d is driven by HRTIM, which Zephyr has no "
			       "driver for - not available in this image", n);
		return PMC_W_REFUSED;
	}
	if (mv > PMC_AO_MAX_MV) {
		(void)snprintk(detail, cap, "%u mV is above the %u mV maximum",
			       (unsigned)mv, (unsigned)PMC_AO_MAX_MV);
		return PMC_W_REFUSED;
	}

	/*
	 * Scaled against the arithmetic full scale, not the 10 V maximum, so a
	 * 10 V request asks for about 92% duty rather than 100%. If the real
	 * transfer differs this is where it gets corrected - once a meter has
	 * said what it actually is.
	 */
	pulse = (uint32_t)(((uint64_t)ao_pwm[ao_slot[n]].period * mv) /
			   PMC_AO_FULL_MV);

	rc = pwm_set_pulse_dt(&ao_pwm[ao_slot[n]], pulse);
	if (rc != 0) {
		(void)snprintk(detail, cap, "pwm_set_pulse failed (%d)", rc);
		return PMC_W_ERROR;
	}

	s_mv[n] = (int32_t)mv;
	(void)snprintk(detail, cap,
		       "AO%d set to %u mV (%u%% duty) - no sense path exists "
		       "on this family and the volts-to-duty transfer is "
		       "unmeasured, so this is a command, not a reading",
		       n, (unsigned)mv,
		       (unsigned)((pulse * 100U) / ao_pwm[ao_slot[n]].period));
	return PMC_W_COMMANDED;
}
