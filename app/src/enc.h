/*
 * The two incremental encoder inputs, decoded in software.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 *
 * WHY SOFTWARE, WHICH IS NOT A PREFERENCE. The STM32's timers can decode
 * quadrature in hardware with no CPU cost at all, and neither channel on this
 * carrier can use it. Encoder mode needs channels 1 and 2 of ONE timer on the
 * A and B inputs; checked against stm32h747xihx-pinctrl.dtsi:
 *
 *   ENC0  A PJ8  = tim8_ch1      B PH12 = tim5_ch3   two different timers,
 *                                                    and CH3 rather than CH2
 *   ENC1  A PC13 = analog ONLY   B PI7  = tim8_ch3   PC13 carries no
 *                                                    alternate function at
 *                                                    all on this package
 *
 * So both are decoded from GPIO interrupts, and that has a ceiling worth
 * knowing before trusting a reading - see PMC_ENC_MAX_EDGES_HINT below.
 *
 * FOUR EDGES PER CYCLE (4x). Every edge of both A and B is counted, so a
 * 1000-line encoder gives 4000 counts per revolution. That is the highest
 * resolution the signals carry and it is also the most interrupts; a 1x
 * decoder would be four times cheaper and throw away three quarters of what
 * the encoder is telling you.
 *
 * WHAT THE INDEX DOES, AND WHAT IT DOES NOT. Z is counted and the position at
 * the moment of the pulse is recorded, but it does NOT zero the count by
 * itself. An index that silently rewrote the position would make a lost count
 * invisible - the one thing this panel exists to show. `pmc_enc_zero()` is
 * explicit, and the distance between successive index pulses is the honest
 * measure of whether anything was missed.
 */
#ifndef PMC_ENC_H
#define PMC_ENC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PMC_ENC_COUNT 2

/*
 * Roughly where software decoding stops keeping up, as an order of magnitude
 * rather than a measurement: a GPIO interrupt on this part costs a few
 * hundred nanoseconds of entry and exit, and the decoder itself is a table
 * lookup. Two channels at 4x means eight edge sources competing for one CPU.
 *
 * A 1000-line encoder at 3000 rpm is 50 kHz of cycles and 200 k edges per
 * second, which is the same order as this number. Past it, counts are LOST
 * and the loss is silent - the state machine still sees legal transitions, it
 * just sees fewer of them. `errors` catches an ILLEGAL transition, which is a
 * different failure, so a clean error count is not evidence of a correct one.
 * Compare index-to-index distance against the encoder's line count instead.
 */
#define PMC_ENC_MAX_EDGES_HINT 200000U

struct pmc_enc_chan {
	int32_t  count;        /* 4x counts since boot or the last zero     */
	int32_t  at_index;     /* count when the last index pulse arrived   */
	int32_t  per_index;    /* counts between the last two index pulses  */
	uint32_t index_hits;   /* index pulses seen                         */
	uint32_t errors;       /* illegal A/B transitions: both changed at
				* once, which means an edge was missed or a
				* line is bouncing                          */
	int32_t  rate;         /* counts per second, over the sample window */
	uint8_t  a, b, i;      /* the three lines as they read right now    */
	bool     ready;        /* the pins configured and interrupts armed  */
};

int  pmc_enc_init(void);
void pmc_enc_read(uint8_t n, struct pmc_enc_chan *out);
bool pmc_enc_zero(uint8_t n, char *detail, size_t cap);

/* Called on the panel's poll to difference the counters into `rate`. */
void pmc_enc_sample(void);

#ifdef __cplusplus
}
#endif

#endif /* PMC_ENC_H */
