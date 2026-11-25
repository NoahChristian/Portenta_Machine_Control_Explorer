/*
 * DIN and PDIO, read from the two TCA6424A expanders.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 *
 * THE CHANNEL MAPS ARE SCRAMBLED THREE DIFFERENT WAYS and they are the whole
 * reason this file exists as data rather than as arithmetic. A channel number
 * is not a bit number here: PDIO 4-7 have their write bits reversed inside the
 * nibble, the PDIO READ bits descend and live in a different port range from
 * the write bits, and the DIN map is neither ascending nor a reversal. Getting
 * one wrong gives a panel that confidently reports the wrong channel - and,
 * once writes exist, energizes the wrong 24 V output.
 *
 * Every map below is [mbed] - transcribed from ArduinoIOExpander.h, which is
 * another program's belief and has not been seen on this board. Nothing here
 * is [measured] yet; pmc_io_read() reports levels and the panel marks them
 * unverified until a wire has been moved one channel at a time.
 *
 * DO IS NOT HERE. The eight digital outputs are H7 GPIOs behind a shared latch
 * (PB_2), not expander bits, so they arrive with the write path rather than
 * with this.
 */
#ifndef PMC_EXPANDER_H
#define PMC_EXPANDER_H

#include <stdbool.h>
#include <stdint.h>

#define PMC_DIN_COUNT  8
#define PMC_PDIO_COUNT 12

/* -1 unknown, 0 low, 1 high. Unknown is NOT low: a bus that did not answer
   and a channel that is off must never render alike. */
struct pmc_io_state {
	bool   din_ok;
	bool   pdio_ok;
	int8_t din[PMC_DIN_COUNT];
	int8_t pdio[PMC_PDIO_COUNT];
	int    din_err;   /* errno from the last DIN read, 0 when fine */
	int    pdio_err;
};

/* Configures both expanders' input pins. Safe to call more than once. */
int pmc_io_init(void);

/* One pass: a single port read per expander, then the maps applied. */
void pmc_io_read(struct pmc_io_state *out);

/* The raw 24-bit input word, for the shell command that confirms a mapping by
   watching which bit actually moves. Returns 0 or a negative errno. */
int pmc_io_raw(bool din_expander, uint32_t *word);

/*
 * THE 24 V RAIL, INFERRED - there is no way to measure it.
 *
 * Datasheet 3-21: the power tree has no divider to any ADC, so neither 24 V
 * nor 12 V can be read. 3-13 gives the one hook: the six encoder channels are
 * pulled up to the BOARD 24 V through 10 k. With J10 idle they all read high
 * exactly when the rail is up, and all six falling together is something a
 * wired encoder cannot do.
 *
 * `present` is true only when all six read high. `wired` is a warning, not a
 * reading: a mixed result means something is driving those pins, so the
 * inference does not apply and the panel must say so rather than show a rail
 * state it cannot justify.
 */
/*
 * THE 24 V INFERENCE, AND THE PART OF IT THAT IS NOT ESTABLISHED.
 *
 * All six encoder inputs read high with nothing plugged into J10, and the
 * firmware takes that to mean the 24 V supply is up. What has never been
 * shown is WHY they read high. No pull-up is configured anywhere - the
 * devicetree sets no bias and both readers use a plain GPIO_INPUT - so
 * whatever holds them up is on the carrier, and nothing in the datasheet
 * extract describes that circuit.
 *
 * **The negative case has never been tested.** Nobody has read these pins
 * with the 24 V supply disconnected. If their bias comes from the logic rail
 * rather than from 24 V, they read high either way and `present` is true
 * always - an indicator that cannot say no. One measurement settles it and it
 * is in the TBD list.
 *
 * Until then this is an inference resting on an assumption, which is why the
 * panel says "inferred" rather than "present" and why the lamp has a third
 * state for cannot-tell.
 */
struct pmc_rail {
	bool    sensed;   /* the pins could be read at all */
	bool    present;  /* all six high - SEE ABOVE before trusting it */
	bool    wired;    /* mixed: an encoder is connected, inference void */
	uint8_t highs;    /* how many of the six read high */
};

void pmc_rail_read(struct pmc_rail *out);

#endif /* PMC_EXPANDER_H */
