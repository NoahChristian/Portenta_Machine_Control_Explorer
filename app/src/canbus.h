/*
 * CAN on the HMI connector, through FDCAN1.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 *
 * NAMED canbus.h AND NOT can.h on purpose: Zephyr's own driver header is
 * <zephyr/drivers/can.h>, and a file called can.h sitting in this directory
 * is a quoted-include away from shadowing it for any translation unit that
 * gets the search order slightly different. The confusion costs more than the
 * three extra characters.
 *
 * TX AND RX ARE SWAPPED RELATIVE TO THE mbed LIBRARY. pins_mc.h records "CAN
 * TX PB_8 RX PH_13"; this package offers only fdcan1_RX on PB8 and only
 * fdcan1_TX on PH13, and no second FDCAN function on either pin. The
 * silicon's own pinctrl table is the authority over another program's header,
 * so this build wires PB8 as RX and PH13 as TX. Still [MBED]-grade and
 * unconfirmed at the connector until a frame has crossed a wire.
 *
 * STB IS ON PA13, WHICH IS ALSO SWDIO. Driving it as a GPIO ends any chance
 * of attaching a debugger over SWD until the next reset - see pmc_can_stb().
 * The Machine Control carrier brings no debug header out, so on this assembly
 * nothing is lost, but on a bare Portenta H7 it would be.
 *
 * ITS POLARITY IS A GUESS AND IS LABELED AS ONE. Nothing on this bench
 * identifies the transceiver, and "STB" is active high on most parts and
 * active low on some. The control reports the LEVEL it drives rather than a
 * claim about what that level means, so the bench can settle it by watching
 * whether frames move.
 *
 * CLASSIC CAN, NOT CAN FD. The controller is FD-capable; the bus, the
 * termination and most dongles are not, and FD would need a data-phase
 * bitrate nobody has specified.
 */
#ifndef PMC_CANBUS_H
#define PMC_CANBUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* How many received frames the panel can show. Small on purpose: this is a
   window onto a live bus, not a logger, and a deep ring would invite being
   read as one. */
#define PMC_CAN_LOG 8

struct pmc_can_frame {
	uint32_t id;
	uint8_t  dlc;
	uint8_t  ext;      /* 29-bit identifier rather than 11-bit */
	uint8_t  rtr;      /* remote transmission request, no data */
	uint8_t  data[8];
	int64_t  stamp;    /* k_uptime_get() when it arrived */
};

struct pmc_can_state {
	bool     present;     /* the controller exists in this build   */
	bool     started;     /* can_start() succeeded                 */
	bool     loopback;    /* internal loopback: no wire involved   */
	uint8_t  stb_level;   /* what the STB pin is being driven to   */
	uint32_t bitrate;
	uint8_t  sample_point_permille_hi; /* reported, not configured */

	/* Zephyr's bus state, which is the thing worth reading before any
	   counter: ERROR_ACTIVE is healthy, BUS_OFF means the controller has
	   excluded itself and will not transmit again until restarted. */
	int      bus_state;
	uint8_t  tx_err_cnt;
	uint8_t  rx_err_cnt;

	uint32_t tx_ok;
	uint32_t tx_fail;
	uint32_t rx_frames;

	/* A single-node CAN bus cannot complete a transmission: with nobody
	   to acknowledge, the controller retries forever and then counts an
	   error. That is the normal result of testing alone, and it is why
	   this is named rather than folded into tx_fail. */
	uint32_t tx_no_ack;
};

int  pmc_can_init(void);
void pmc_can_state_read(struct pmc_can_state *out);
size_t pmc_can_log_read(struct pmc_can_frame *out, size_t cap);

/* Send one classic frame. `hex` is up to 8 bytes already decoded. */
bool pmc_can_send(uint32_t id, bool ext, const uint8_t *data, uint8_t len,
		  char *detail, size_t cap);

/* Stop, switch mode, start again. Loopback needs no bus and no second node. */
bool pmc_can_set_loopback(bool on, char *detail, size_t cap);

/* Change the arbitration bitrate. Stops and restarts the controller. */
bool pmc_can_set_bitrate(uint32_t bps, char *detail, size_t cap);

/* Drive the transceiver's standby pin. See the header note: the level is
   reported, its meaning is not asserted. */
bool pmc_can_stb(bool high, char *detail, size_t cap);

/* Clear BUS_OFF by restarting the controller. */
bool pmc_can_recover(char *detail, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* PMC_CANBUS_H */
