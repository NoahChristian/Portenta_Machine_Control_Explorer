/*
 * Driving the digital outputs, and saying honestly what that did.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 *
 * TWO FAMILIES, AND ONLY ONE OF THEM CAN BE VERIFIED.
 *
 *   DO   eight H7 GPIOs behind the shared latch PB_2, driving 2x TPS4H160
 *        high-side switches. pins_mc.h defines DO0..DO7 and the latch and
 *        NOTHING ELSE - no diagnostic, no fault, no current sense. Reading
 *        back a GPIO that is configured as an output returns the value just
 *        written, which verifies nothing at all. So a DO write reports
 *        `commanded` and never `verified`.
 *
 *   PDIO twelve expander bits, written through one map and read through a
 *        DIFFERENT one - separate bits in a separate port range. That makes
 *        it the only family on this board with a real read-back, and the
 *        strongest verify available. A verify that reused the WRITE map
 *        would confirm a write against the bit it just set, which is the
 *        failure that looks exactly like success.
 *
 * AND THE SUPPLY IS A THIRD ANSWER. Both connectors carry their own `24V IN`
 * which must be fed externally (datasheet 3-4 and 3-5); it is the source rail
 * for the high-side switches, not a pull-up. With it unfed a PDIO channel
 * commanded high reads back low - correctly. That is `no supply`, not a
 * fault and not a mismatch, and rendering it as either would send someone
 * looking for a broken board instead of a missing wire.
 */
#ifndef PMC_IO_WRITE_H
#define PMC_IO_WRITE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


enum pmc_write_result {
	PMC_W_OK = 0,       /* PDIO: commanded and read back as commanded */
	PMC_W_COMMANDED,    /* DO: driven; no read-back path exists to check */
	PMC_W_NO_SUPPLY,    /* read back low with no fault: 24V IN unfed */
	PMC_W_MISMATCH,     /* read back the wrong way with supply present */
	PMC_W_REFUSED,      /* not a channel, or not writable */
	PMC_W_ERROR,        /* the bus or a pin failed */
};

const char *pmc_write_result_str(enum pmc_write_result r);

/* Configures the DO pins and the two latches. Call once from main(). */
int pmc_io_write_init(void);

/*
 * Drive one channel by NAME - "DO3", "PDIO11". The name is the allow-list
 * key: nothing here takes an index or a port bit from outside, so a request
 * for a channel that does not exist is refused rather than aimed somewhere.
 *
 * `detail` receives a sentence for the panel. Returns the verdict.
 */
enum pmc_write_result pmc_io_write(const char *chan, bool on,
				   char *detail, size_t cap);

/* Pulse one channel: on for `ms`, then off. Same verdict rules. */
enum pmc_write_result pmc_io_pulse(const char *chan, uint32_t ms,
				   char *detail, size_t cap);


#ifdef __cplusplus
}
#endif

#endif /* PMC_IO_WRITE_H */
