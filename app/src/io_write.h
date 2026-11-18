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
 * AND A THIRD ANSWER IS NEEDED, BUT IT MUST NOT NAME A CAUSE. Both connectors
 * carry their own `24V IN` which must be fed externally (datasheet 3-4 and
 * 3-5); it is the source rail for the high-side switches, not a pull-up. With
 * it unfed a PDIO channel commanded high reads back low - correctly - and
 * calling that a fault would send someone looking for a broken board instead
 * of a missing wire.
 *
 * THIS USED TO ANSWER `no supply` AND SAY "J8 24V IN is unfed" AS A FACT.
 * Nothing on this board senses that rail. The encoder pull-ups in expander.h
 * sense the BOARD 24 V, fed at the PWR SUPPLY block - a different rail from
 * either output connector's own 24V IN - so `rail24Inferred.present` being
 * true, as it is on this bench, says nothing whatever about J8. Meanwhile the
 * PDIO bit maps are transcribed from the mbed library and confirmed at no
 * connector, which makes a wrong map at least as likely a cause as a missing
 * wire, and it is the hazard this whole panel is built to flag.
 *
 * So the answer is `unexplained` and it names both candidates without
 * choosing between them. A panel that guesses a cause is a panel that sends
 * someone to the wrong end of the problem with confidence.
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
	PMC_W_UNEXPLAINED,  /* commanded high, reads low; cause not knowable */
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

/*
 * The eight DO logic lines, read from the GPIO registers - not from a shadow
 * variable, so a pad that cannot follow its own output register shows up.
 *
 * Defined from reset onward: pmc_io_write_init() drives all eight
 * de-energized before anything else runs, so there is no "unknown" state to
 * report. out[i] is 1, 0, or -1 if that port is not ready. Returns 0, or a
 * negative errno if any channel could not be read.
 */
int pmc_do_read(int8_t *out, size_t n);


#ifdef __cplusplus
}
#endif

#endif /* PMC_IO_WRITE_H */
