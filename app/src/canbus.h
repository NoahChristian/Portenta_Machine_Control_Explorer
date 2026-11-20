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

/*
 * How many received frames the ring holds.
 *
 * Was 8, which is a window and not a buffer: a monitor polling every 400 ms
 * against a frame every 100 ms loses half of them and only finds out through
 * the `lost` flag. 32 covers a few seconds of moderate traffic at about a
 * kilobyte of RAM, and it is still a WINDOW - this is a bench instrument, not
 * a logger, and nothing here pretends a busy bus can be followed completely.
 */
#define PMC_CAN_LOG 32

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

	/*
	 * A SINGLE-NODE CAN BUS CANNOT COMPLETE A TRANSMISSION, and measuring
	 * that changed this struct.
	 *
	 * The ACK bit must be driven by a DIFFERENT node: the transmitter
	 * sends recessive in the ACK slot and cannot answer itself. Alone on a
	 * bus, the frame is never acknowledged and is retried - and measured
	 * on this board, the controller goes ERROR_PASSIVE with a transmit
	 * error count of 128 and STAYS there. It does not reach bus-off,
	 * because an error-passive transmitter does not increment its counter
	 * on an acknowledgment error. So it retransmits indefinitely.
	 *
	 * Which means the frame NEVER COMPLETES, so neither completion path
	 * runs and tx_no_ack never moves. Reporting "0 unacknowledged" while
	 * the controller sits in ERROR_PASSIVE is worse than reporting
	 * nothing. `tx_queued` minus the three outcomes is what is still in
	 * flight, and that is the number that tells the story.
	 */
	uint32_t tx_queued;
	uint32_t tx_no_ack;

	/*
	 * SENDS THE CONTROLLER NEVER ACCEPTED, counted apart from the ones it
	 * accepted and then could not complete - and the separation is not
	 * tidiness, it is what makes the subtraction above valid.
	 *
	 * Measured: with a CANopen heartbeat producing one frame a second
	 * into a bus with no second node, the first frame occupies a TX
	 * mailbox forever and every send after it returns -EAGAIN
	 * immediately. Those were counted in tx_fail, which is subtracted
	 * from tx_queued - but a rejected send was never queued, so the
	 * difference went NEGATIVE and the shell reported
	 * "**4294966629 STILL IN FLIGHT**". Six hundred rejections also hid
	 * inside a number that reads as "the controller is broken".
	 *
	 * So: tx_reject is before the queue, tx_fail is after it, and
	 * queued - ok - no_ack - fail cannot go below zero.
	 */
	uint32_t tx_reject;

	/*
	 * Is the TX pin actually moving? Sampled with the CPU, the same way
	 * PA1 was checked for the Ethernet reference clock. It cannot measure
	 * a bit rate and does not try: it separates "the controller is
	 * driving the transceiver" from "nothing is happening at all", which
	 * is the question a lone node on a silent bus actually has.
	 *
	 * IT ALSO SETTLED WHICH LOOPBACK THIS IS, against a wrong reading of
	 * the driver. can_mcan_set_mode() sets TEST.LBCK and leaves CCCR.MON
	 * clear, which by the M_CAN specification is EXTERNAL loop back - the
	 * TX pin driven, the RX pin ignored. The pin says otherwise:
	 * `can txprobe` sampled PH13 144,000 times across 48 loopback frames
	 * and never once saw it low, while the same sampler shows it toggling
	 * in normal mode. So this is INTERNAL loop back and nothing reaches
	 * the transceiver. Why the register bits do not produce what the
	 * specification describes is unexplained; the measurement is not.
	 */
	uint8_t  tx_pin_toggling;
	uint8_t  tx_pin_sampled;
};

int  pmc_can_init(void);
void pmc_can_state_read(struct pmc_can_state *out);
size_t pmc_can_log_read(struct pmc_can_frame *out, size_t cap);

/*
 * The receive window as a CURSOR, so a monitor can follow the bus without
 * missing frames or replaying old ones - the same shape as
 * pmc_rs485_rx_read(), and for the same reason.
 *
 * `from` is a frame index, not a position in the ring. Pass 0 to start at the
 * beginning, then pass back whatever `next` returned. `lost` is set when the
 * window asked for has already been overwritten, which is the one thing a
 * monitor must be told rather than left to infer from a gap in the data.
 */
size_t pmc_can_rx_read(uint64_t from, struct pmc_can_frame *out, size_t cap,
		       uint64_t *next, bool *lost);

/*
 * The index the NEXT received frame will take.
 *
 * For a caller that wants to watch what happens from HERE ON, which is what
 * a test does: take the cursor, do the thing, then read forward from it.
 * Without this the only way to find the current position is to read the whole
 * window and count, which races against the frame that arrives while you are
 * counting.
 */
uint64_t pmc_can_rx_cursor(void);

/*
 * A TAP ON THE RECEIVE PATH, for a protocol that has to ANSWER frames rather
 * than display them.
 *
 * ONE tap, set once at init. Not a subscriber list: a list is a lifetime
 * problem and an ordering problem, and this file has exactly one consumer
 * above it. A second one is a reason to write the list then, with the
 * question of what happens when the first one answers a frame the second one
 * also wanted already answered.
 *
 * IT RUNS IN THE DRIVER'S RECEIVE CALLBACK, WHICH IS INTERRUPT CONTEXT on
 * this controller. So it must copy the frame and return - no logging, no
 * sending, no mutex, nothing that can block. The CANopen node queues the
 * frame to its own thread and does every reply from there, which is also why
 * the frame it is handed is a copy rather than a pointer into the ring: the
 * ring entry can be overwritten before the handler thread ever runs.
 */
typedef void (*pmc_can_rx_tap_t)(const struct pmc_can_frame *f);
void pmc_can_rx_tap_set(pmc_can_rx_tap_t fn);

/*
 * Repeat a frame every `period_ms` until stopped with period_ms = 0.
 *
 * A TRAFFIC SOURCE, so one console can make the bus busy while another
 * watches it. The RS485 side has had this since it was written and it is the
 * single most useful thing on that panel; CAN wants it for the same reason -
 * a bus you have to keep poking by hand is a bus you cannot watch.
 */
bool pmc_can_repeat(uint32_t id, bool ext, const uint8_t *data, uint8_t len,
		    uint32_t period_ms, char *detail, size_t cap);
void pmc_can_repeat_get(uint32_t *period_ms, uint32_t *sent);

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
