/*
 * One pass over the hardware, taken when a page is rendered.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 *
 * WHY A SNAPSHOT AND NOT READERS CALLED FROM THE TEMPLATE. The page is
 * rendered server-side by HtmlTpl, which calls a row provider once per row -
 * and a provider that read the ADC on each call would make the three analog
 * rows three different instants, with the table silently implying they were
 * one. Worse, the panel's whole claim is "every reading on this page was taken
 * when the page was rendered". A snapshot is what makes that sentence true
 * rather than approximately true.
 *
 * It is also what keeps the two views honest. The page and /state.json render
 * from THIS struct, so they cannot disagree: there is one set of readers in
 * snapshot.c and nothing else in the application touches the ADC or the image
 * headers. A second reader for the JSON would be a second thing to keep in
 * step, and the first time they drifted the panel and the tooling would
 * describe different boards.
 */
#ifndef PMC_SNAPSHOT_H
#define PMC_SNAPSHOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "clock.h"
#include "expander.h"

#define PMC_SNAP_AI   3
#define PMC_SNAP_SLOT 2

struct pmc_ai_read {
	const char *name;
	const char *pad;
	int16_t     raw;
	int32_t     mv;
	bool        open; /* the SYSCFG analog switch is OPEN, i.e. the pad is
			   * not tied to the Ethernet clock or SPI2 - see
			   * boards/shields/.../pmc_syscfg.c */
	bool        ok;   /* the read succeeded; raw and mv are meaningless if
			   * this is false, and are reported as -1 */
};

struct pmc_slot_read {
	uint8_t slot;
	char    version[24];
	bool    present;
	bool    active;
	bool    confirmed;
};

/*
 * WHETHER THERE IS A CLOCK, AND WHY NOT.
 *
 * The carrier's PCF8563 reports its own trustworthiness and that is better
 * than anything this application could infer. The VL bit in its seconds
 * register means the oscillator stopped, and the driver turns that into
 * -ENODATA from rtc_get_time(). A 100 mF supercapacitor holds the part up for
 * at least 48 hours with board power removed (datasheet section 3-20), so
 * these three states are the difference between switching the bench on
 * overnight and switching it on after a long weekend.
 *
 * ABSENT and LOST must not render the same, for the same reason a channel
 * with no reader must not render as LOW: one is a missing part and the other
 * is a part saying it lost time.
 */
enum pmc_clock_state {
	PMC_CLOCK_ABSENT = 0, /* no device in the tree, or it never answered */
	PMC_CLOCK_LOST,       /* answered -ENODATA: the oscillator had stopped */
	PMC_CLOCK_HELD,       /* read cleanly: the supercapacitor carried it */
};

struct pmc_snapshot {
	const char *host;
	const char *fw;
	uint32_t    uptime_s;

	uint8_t clock_state;  /* enum pmc_clock_state */
	int64_t clock_epoch;  /* UTC seconds; 0 unless clock_state is HELD */
	struct pmc_clock_drift drift;  /* what NTP has measured, if anything */
	struct pmc_io_state    io;     /* DIN and PDIO off the expanders */
	struct pmc_rail        rail;   /* 24 V, inferred - never measured */

	struct pmc_ai_read   ai[PMC_SNAP_AI];
	size_t               n_ai;
	struct pmc_slot_read slot[PMC_SNAP_SLOT];
	size_t               n_slot;
};

/* From shell_mcuboot.c - the swap state, which the panel is now the only
   way to see. */
const char *pmc_swap_type_name(void);
bool pmc_img_confirmed(void);

/* Reads everything once. Call it before rendering, never during. */
void pmc_snapshot_take(struct pmc_snapshot *s);

/*
 * The reading for one named channel, written into `out`.
 *
 * Returns the logic level where there is one: 1 high, 0 low, -1 unknown. Only
 * the analog channels answer anything today; DIN, DO and PDIO are TCA6424A
 * bits that nothing reads until M2, so they report unknown and `out` says so
 * in words. THEY DO NOT REPORT LOW. A channel with no reader and a channel
 * reading 0 V must not render identically, because the second is a
 * measurement and the first is an absence.
 */
int pmc_snapshot_reading(const struct pmc_snapshot *s, const char *chan,
			 char *out, size_t cap);

/* Serializes the snapshot as JSON. Returns the byte count written. */
size_t pmc_snapshot_json(const struct pmc_snapshot *s, char *out, size_t cap);

#endif /* PMC_SNAPSHOT_H */
