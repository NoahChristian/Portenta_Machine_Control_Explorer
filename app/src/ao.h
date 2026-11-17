/*
 * Analog out: a PWM, a filter and an amplifier - not a DAC.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 *
 * Datasheet 3-7: each channel is a PWM through a double low-pass filter into a
 * non-inverting amplifier of gain 3.3. So the terminal voltage is a function
 * of duty cycle, and resolution and settling are properties of the filter.
 *
 * THE VOLTS-TO-DUTY TRANSFER IS NOT MEASURED. A 3.3 V logic swing through a
 * gain of 3.3 implies about 10.9 V at full duty, which is where the scaling
 * below comes from - but that is arithmetic from two datasheet numbers, not a
 * reading. Until a meter has been on a terminal, every setpoint reports `set
 * (not verified)` and the panel says the transfer is unmeasured.
 *
 * AND THERE IS NO READ-BACK AT ALL. No sense path exists on any AO channel, so
 * `verified` is not reachable here even in principle. Confirming a setpoint
 * means a meter, or looping the output into an analog input.
 *
 * AO2 IS ABSENT. It is PG_7, driven by HRTIM - a peripheral Zephyr has no
 * driver for and which carries no tim*_ch* function in the pinctrl file.
 */
#ifndef PMC_AO_H
#define PMC_AO_H

#include <stdbool.h>
#include <stdint.h>

#include "io_write.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PMC_AO_COUNT   4
#define PMC_AO_MAX_MV  10000U

/* Full scale at 100% duty, from 3.3 V x 3.3. Arithmetic, not a measurement. */
#define PMC_AO_FULL_MV 10890U

int pmc_ao_init(void);

/* Set one channel by name - "AO0". Refuses AO2 and anything out of range. */
enum pmc_write_result pmc_ao_set(const char *chan, uint32_t mv,
				 char *detail, size_t cap);

/* Last commanded millivolts, or -1 for a channel that cannot be driven. */
int32_t pmc_ao_commanded(int n);

/* True when the channel exists in this build (false for AO2). */
bool pmc_ao_available(int n);

#ifdef __cplusplus
}
#endif

#endif /* PMC_AO_H */
