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

/* Bytes kept of each direction. One Modbus RTU frame is at most 256. */
#define PMC_485_RING 2048

/* The longest frame the repeater will send. */
#define PMC_485_TX_MAX 64

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
void pmc_rs485_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* PMC_RS485_H_ */
