/*
 * A minimal CANopen node, so a standard tool sees a real device.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 *
 * WHY THIS EXISTS. `can send` proves a controller. It does not answer the
 * question anyone with a CAN tool actually asks, which is "is there a node on
 * this bus". CANopen is what the industrial world scans for, and the alive
 * part of it is small: a boot-up message, a heartbeat, four NMT commands, and
 * an expedited SDO server over a handful of mandatory objects. That is what
 * is here. `candump`, `cansend` and python-canopen see this node, scan it,
 * start and stop it, and read its identity.
 *
 * WHAT THIS IS NOT. It is not a CANopen stack and must not be mistaken for
 * one. There are no PDOs, no object dictionary beyond the five entries below,
 * no segmented or block SDO transfer, no SYNC, no EMCY, no heartbeat
 * CONSUMER, no LSS, no node guarding and no CiA device profile. A real stack
 * - CANopenNode is the obvious one - is 15 to 25 thousand lines of C, and the
 * hard parts of it are exactly the parts left out here. The honest
 * description is "a node that answers": enough to prove the wiring, the bit
 * rate, the termination and the identifiers against a real tool, which is the
 * step that has to come first anyway.
 *
 * IT CANNOT PROVE A BUS BY ITSELF, and the self-test says so in its own
 * output. A CAN transmitter cannot acknowledge its own frame - the ACK bit
 * must be driven by a different node - so a single node on a wire parks in
 * ERROR_PASSIVE and retransmits forever. What `canopen selftest` does instead
 * is run the whole node through INTERNAL loopback, where frames come straight
 * back: that exercises the state machine, the NMT decode, the SDO server, the
 * object dictionary and the heartbeat cadence, and touches nothing past the
 * MCU. The receive path off a real wire remains unmeasured, and it is in the
 * TBD list in docs/MEASURED.md.
 *
 * THE NODE ID IS NOT PERSISTED. It comes from Kconfig and can be changed at
 * runtime, and a reset puts it back. That is deliberate for now: the
 * self-test asserts against a known identity, and a remembered node id is a
 * setting somebody would have to look up before trusting any of its output.
 * Persisting it belongs with the rest of the remembered set in persist.c, and
 * is a decision rather than an omission.
 */
#ifndef PMC_CANOPEN_H
#define PMC_CANOPEN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The NMT states, and the values ARE the heartbeat bytes.
 *
 * CiA 301 uses one encoding for the state a heartbeat reports and another for
 * the command that causes it, which is the kind of near-miss that produces a
 * node reporting 0x01 because somebody reused the start command's number. The
 * numbers below are the heartbeat side, taken from the specification, and the
 * command constants live in canopen.c where they are used.
 */
enum pmc_co_state {
	PMC_CO_BOOTUP = 0x00,  /* only ever sent once, as the boot-up message */
	PMC_CO_STOPPED = 0x04,
	PMC_CO_OPERATIONAL = 0x05,
	PMC_CO_PREOP = 0x7F,
};

struct pmc_co_state_read {
	bool     enabled;      /* compiled in and initialized */
	uint8_t  node;         /* 1..127 */
	uint8_t  state;        /* enum pmc_co_state */
	uint16_t hb_ms;        /* producer heartbeat period; 0 = off */

	uint32_t boots;        /* boot-up messages sent, including the first */
	uint32_t hb_sent;
	uint32_t nmt_rx;       /* NMT commands accepted */
	uint32_t nmt_ignored;  /* NMT frames addressed to another node */
	uint32_t sdo_rx;       /* SDO requests addressed to this node */
	uint32_t sdo_ok;       /* answered with data or a download response */
	uint32_t sdo_abort;    /* answered with an abort */
	uint32_t dropped;      /* frames the handler queue could not take */

	/* The identity object, so one place reports what a tool would read. */
	uint32_t vendor;
	uint32_t product;
	uint32_t revision;
	uint32_t serial;
};

int  pmc_canopen_init(void);
void pmc_canopen_state_read(struct pmc_co_state_read *out);

/* Drive the state machine locally, as an NMT master on the bus would. */
bool pmc_canopen_nmt_local(uint8_t cmd, char *detail, size_t cap);

bool pmc_canopen_node_set(uint8_t node, char *detail, size_t cap);
bool pmc_canopen_hb_set(uint16_t ms, char *detail, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* PMC_CANOPEN_H */
