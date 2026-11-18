/*
 * RS485/RS232: a terminal, not a protocol stack.
 *
 * This deliberately does NOT implement Modbus. The complaints about Modbus on
 * this hardware are almost never about the protocol - they are about not being
 * able to see whether a single byte ever left the board. A library returns
 * "Connection timed out" and there is nowhere to look. So this is a wire-level
 * terminal: what went out, what came back, and every transceiver control the
 * carrier actually has.
 */
#ifndef PMC_RS485_H_
#define PMC_RS485_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * THE RESIDENT RECEIVE RING. Sizes the RX path only; the transmit side is one
 * frame at a time, bounded by PMC_485_TX_MAX below.
 *
 * Deliberately small. This is what the board costs when nobody has asked for
 * anything, and it is not a guess at what a session needs: 2048 bytes is
 * 178 ms at 115200 8N1, which is under one of the panel's 400 ms polls, so a
 * continuously busy line at that speed raises `lost`. That is the honest
 * default - a user who needs more arms a capture and is told what it costs.
 * See docs/ARCHITECTURE.md, "Memory and processing constraints".
 */
#define PMC_485_RESIDENT 2048

/* An armed capture is refused outside these bounds before the heap is asked. */
#define PMC_485_CAP_MIN  4096
#define PMC_485_CAP_MAX  (1024 * 1024)

/*
 * Arm a capture: take `bytes` from the system heap and receive into it until
 * released. Returns 0 on success or a negative errno, and writes a sentence to
 * `why` EITHER WAY - on refusal it names what was asked for, what is free, and
 * what is holding the rest, because that refusal is the information the user
 * came for.
 *
 * Arming discards what is already buffered: absolute positions are kept, but
 * the bytes behind them lived at a different modulus, so the next read reports
 * `lost` once and the reader resynchronizes.
 */
int  pmc_rs485_cap_arm(size_t bytes, char *why, size_t whycap);

/* Release an armed capture and fall back to the resident ring. Safe to call
 * when nothing is armed. Also writes a sentence to `why`. */
void pmc_rs485_cap_release(char *why, size_t whycap);

/* The budget, for the panel. `armed` is 0 when running on the resident ring. */
void pmc_rs485_cap_info(size_t *armed, size_t *resident, size_t *heap_free,
			size_t *heap_total);

/* The longest frame the repeater will send. A Modbus RTU ADU is 256 bytes, so
 * anything smaller cannot express a full write-multiple-registers request.
 */
#define PMC_485_TX_MAX 256

enum pmc_485_mode {
	PMC_485_HALF = 0,  /* two wires, driver and receiver share them */
	PMC_485_FULL,      /* four wires */
	PMC_485_RS232,     /* the transceiver's other personality */
};

struct pmc_485_cfg {
	uint32_t baud;
	uint8_t  data_bits;   /* 7 or 8 */
	char     parity;      /* 'N', 'E', 'O' */
	uint8_t  stop_bits;   /* 1 or 2 */
	enum pmc_485_mode mode;
	bool     term;        /* 120 ohm termination across the pair */
	bool     slew;        /* slew-rate limiting, for long cable */
	bool     echo;        /* leave the receiver on while transmitting */
};

int pmc_rs485_init(void);

/*
 * DIAGNOSTIC: drive one transceiver control pin directly.
 *
 * Exists because polarity is not guessable and guessing costs a flash each
 * time. /RE on this board turned out to be active low after this code had
 * driven it high all session, and the board was deaf the whole time with
 * nothing able to say so - an idle bus and a muted receiver produce the same
 * evidence, which is none. Rather than repeat that discovery once per pin,
 * every control line is reachable from the shell so the right combination can
 * be swept rather than argued about.
 *
 * Names: de, re, en, rs232, term, fdterm, slew, hf.
 */
bool pmc_rs485_pin_set(const char *name, int value, char *detail, size_t cap);

/*
 * DIAGNOSTIC: shift bytes out of the UART and touch NOTHING else - no driver
 * enable, no receiver enable, no turnaround. Whatever the control pins were
 * left at is what the transceiver sees.
 *
 * This exists so that pin polarity can be established by experiment instead
 * of by argument. pmc_rs485_send() asserts DE itself, which makes it useless
 * for deciding whether asserting DE is the right thing to do - the test and
 * the assumption are the same line of code. This separates them.
 */
int pmc_rs485_send_raw(const uint8_t *buf, size_t len, char *detail,
		       size_t cap);

void pmc_rs485_cfg_get(struct pmc_485_cfg *out);
bool pmc_rs485_cfg_set(const struct pmc_485_cfg *in, char *detail, size_t cap);

/*
 * The turnaround time this configuration implies, in microseconds: 3.5
 * character times, which is what Modbus RTU calls a frame boundary. Reported
 * so the number is visible rather than buried - getting it wrong is the most
 * commonly reported cause of silence on this bus.
 */
uint32_t pmc_rs485_char_us(void);
uint32_t pmc_rs485_turnaround_us(void);

/* Send bytes once. Returns how many went out, or negative on error. */
int pmc_rs485_send(const uint8_t *buf, size_t len, char *detail, size_t cap);

/* The repeating transmitter. period_ms of 0 stops it. */
bool pmc_rs485_repeat_set(const uint8_t *buf, size_t len, uint32_t period_ms,
			  char *detail, size_t cap);
void pmc_rs485_repeat_get(uint8_t *buf, size_t cap, size_t *len,
			  uint32_t *period_ms);

/*
 * Read received bytes starting at absolute position `from`. Returns how many
 * were copied and sets *next to the position to ask for next time. If `from`
 * is older than the ring still holds, *lost is set and reading resumes at the
 * oldest byte still present - a dropped window is reported, never papered
 * over.
 */
size_t pmc_rs485_rx_read(uint64_t from, uint8_t *out, size_t cap,
			 uint64_t *next, bool *lost);

uint64_t pmc_rs485_rx_total(void);
uint64_t pmc_rs485_tx_total(void);
uint32_t pmc_rs485_errors(void);

/*
 * Milliseconds since the last byte each way, or -1 for never. The panel's
 * TX/RX lamps need "recently", which a running total cannot express.
 */
void pmc_rs485_activity(int32_t *tx_age_ms, int32_t *rx_age_ms);
void pmc_rs485_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* PMC_RS485_H_ */
