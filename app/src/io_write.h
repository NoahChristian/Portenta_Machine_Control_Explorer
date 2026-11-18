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

/*
 * PULSE IS A MODE, NOT ONE SHOT, AND IT PUTS THE CHANNEL BACK.
 *
 * It used to drive high, k_msleep(ms) on the HTTP server thread, then drive
 * LOW - which blocked the one thread every page shares, and left the channel
 * off whatever it had been before. Turning a pulse off erased the state the
 * channel had been commanded into, with nothing saying it had.
 *
 * Now `arm` starts it blinking on its own thread - PMC_PULSE_HIGH_MS high
 * every PMC_PULSE_PERIOD_MS - and disarming RESTORES the level the channel
 * held when it was armed. An explicit pmc_io_write() on a pulsing channel
 * disarms it and wins, because a direct command should not be fighting a
 * blinker.
 *
 * DO only. PDIO's write costs an I2C transaction and a settle, which is not
 * something to repeat forever on a timer for a lamp effect.
 */
#define PMC_PULSE_HIGH_MS    200U
#define PMC_PULSE_PERIOD_MS 1000U

enum pmc_write_result pmc_io_pulse(const char *chan, bool arm,
				   char *detail, size_t cap);

/* Bit n set means DO n is currently pulsing. The panel animates from this
   rather than sampling a 1 Hz blink with a 2 s poll, which would alias. */
uint32_t pmc_io_pulse_mask(void);

/*
 * WHAT PDIO WAS COMMANDED TO, which is NOT what pmc_io_read() reports.
 *
 * The read map is a different set of bits in a different port range, and it
 * reads the terminal - so with the connector's own 24V IN unfed, or with a
 * mis-transcribed map, a channel commanded high reads low. Both facts are
 * worth showing, and the lamp shows this one: a lamp that follows the
 * read-back tells you about the wiring when you asked about the command.
 *
 * out[i] is 1, 0, or -1 for a channel this image has never written - PDIO
 * pins are left as inputs until first use, so before that there is no
 * commanded level, and -1 is not "off".
 */
int pmc_pdio_commanded(int8_t *out, size_t n);

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
