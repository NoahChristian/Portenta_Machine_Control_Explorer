/*
 * The wall clock: the carrier's RTC, disciplined by NTP.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 *
 * FOUR STEPS, AND THE THIRD IS THE ONLY INTERESTING ONE.
 *
 *   1. At boot, read the RTC. The part answers for itself: the VL bit in its
 *      seconds register means the oscillator stopped, which the driver turns
 *      into -ENODATA. A clean read means the supercapacitor carried the time
 *      across a power-off and the board already knows what time it is, with
 *      no network involved.
 *   2. If it has no time and NTP answers, set it, and remember the moment it
 *      was set.
 *   3. Later - hours, not minutes - ask NTP again and read the RTC beside it.
 *      The error is the difference, and the rate follows from the interval.
 *   4. Report that error, with the span it was measured over, next to the
 *      time and where the time came from.
 *
 * NOTHING HERE MEASURES ITSELF, which is why it needs no second time base.
 * The thing being measured is an independent part read over I2C and the
 * reference is NTP. (The failure that machinery would guard against is real
 * but belongs to a different design: disciplining the SYSTEM clock from NTP
 * and then trying to measure its rate between syncs cannot work, because the
 * sync steps the very clock under measurement. This does not do that - the
 * system clock is never set here at all.)
 *
 * PPM IS CARRIED AS INTEGER TENTHS, not a float. The RTC reads whole seconds,
 * so the measurement is integer to begin with, and keeping it that way means
 * no part of this needs CONFIG_CBPRINTF_FP_SUPPORT to print a number.
 */
#ifndef PMC_CLOCK_H
#define PMC_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

/*
 * The drift measurement, or the absence of one.
 *
 * `valid` is false until a span long enough to mean anything has elapsed since
 * the RTC was set. That matters more than it looks: the part reports whole
 * seconds, so over one hour a +-20 ppm crystal moves 72 ms and the measurement
 * reads zero no matter what the oscillator does. A ppm figure without the span
 * it was taken over is not a measurement, so the span travels with it and the
 * panel renders both.
 */
struct pmc_clock_drift {
	bool     valid;
	int32_t  ppm_tenths;  /* + = the RTC runs fast against UTC */
	uint32_t span_s;      /* interval the figure was measured over */
	int64_t  set_epoch;   /* UTC second the RTC was last written; 0 = never */
	uint32_t syncs;       /* successful NTP queries since boot */
	uint32_t failures;    /* queries that did not answer */
};

/* Starts the sync thread. Call once from main(), before the network is up. */
void pmc_clock_init(void);

/*
 * Told that an address is configured. Only gives a semaphore, so it is safe
 * from the workqueue that raises it - see the stack note in httpd.c for what
 * happens when something heavier is called from there.
 */
void pmc_clock_net_up(void);

/* The current measurement, for the panel and for /state.json. */
void pmc_clock_get(struct pmc_clock_drift *out);

#endif /* PMC_CLOCK_H */
