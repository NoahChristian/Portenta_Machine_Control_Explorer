/*
 * Settings that survive a power cycle.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 *
 * WHAT IS REMEMBERED, AND WHAT IT COSTS. Zephyr's settings subsystem over NVS
 * in the QSPI `kvs` partition - the same store the WiFi credentials already
 * use, which is why settings_subsys_init() is called here too and is safe to
 * call twice. Two records, because they are restored at two DIFFERENT TIMES
 * and for different reasons:
 *
 *   pmc/cfg   RS485 line settings, temperature mode and wire count.
 *             Restored during init. None of it energizes anything, so there
 *             is nothing to wait for.
 *
 *   pmc/out   The eight digital output levels, the twelve PDIO commanded
 *             levels, and the pulse mask.
 *             Restored ONLY once every health check has passed.
 *
 * WHY THE OUTPUTS WAIT. docs/SAFETY.md's interlock (0) drives every output
 * de-energized in the earliest init, before networking, so that a reboot or
 * an OTA revert cannot leave 24 V in whatever state another image set.
 * Remembering outputs is in tension with that, and the resolution chosen is
 * to keep the interlock and restore afterwards: an image that crash-loops, or
 * one that is about to be reverted because it never got healthy, NEVER
 * re-energizes anything. A board that has proved itself for 60 seconds with
 * its expanders answering and its panel serving is a different proposition
 * from one that has just been handed the bus.
 *
 * So the sequence at every boot is: everything low, then prove it, then put
 * back what was set. The gap is at least the health gate's 60 s minimum.
 *
 * FLASH WEAR IS REAL AND THE SAVE IS DEFERRED. NVS writes to a 4 KB sector in
 * the QSPI part. Every control on the panel calls pmc_persist_note() and that
 * only schedules - the actual write happens once, PMC_PERSIST_HOLD_MS after
 * the last change, so dragging a row of outputs on and off costs one record
 * rather than twelve. Nothing calls settings_save_one() directly.
 *
 * A LAYOUT CHANGE MUST NOT RESTORE GARBAGE. An OTA can change these structs
 * while NVS still holds the previous image's bytes, and a struct read back at
 * the wrong size or with fields shifted would energize outputs nobody chose.
 * Both records carry a version byte and are rejected whole on a mismatch or a
 * wrong length - falling back to "nothing remembered", which is the safe
 * direction. Bump PMC_PERSIST_VER whenever either struct changes.
 */
#ifndef PMC_PERSIST_H
#define PMC_PERSIST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bump on ANY change to the stored structs in persist.c. */
#define PMC_PERSIST_VER     1

/* How long after the last change the record is written. */
#define PMC_PERSIST_HOLD_MS 3000

/*
 * Load both records and apply the configuration half. Call once, after the
 * drivers it configures are initialized and before the panel is served.
 * Outputs are NOT applied here - see pmc_persist_restore_outputs().
 */
int pmc_persist_init(void);

/*
 * Something a person set has changed. Schedules a write; cheap to call from
 * any thread and from any number of places. The actual snapshot is taken when
 * the timer fires, not here, so this never has to be given the new value.
 */
void pmc_persist_note(void);

/*
 * Put the remembered outputs back. Called from the health thread at the
 * instant every check passes, and does nothing on a boot with no stored
 * record. Returns the number of channels it drove, or a negative errno.
 *
 * Idempotent: a second call does nothing, so a health gate that passes twice
 * cannot re-drive an output a person has since switched off by hand.
 */
int pmc_persist_restore_outputs(void);

/* For the panel: what is stored and whether it has been put back yet. */
struct pmc_persist_state {
	bool    have_cfg;      /* a configuration record was loaded      */
	bool    have_out;      /* an output record was loaded            */
	bool    restored;      /* the outputs have been put back         */
	bool    rejected;      /* a record was present but unreadable    */
	uint8_t stored_ver;    /* what the rejected record claimed to be */
	uint32_t saves;        /* records written since boot             */
};

void pmc_persist_get(struct pmc_persist_state *out);

#ifdef __cplusplus
}
#endif

#endif /* PMC_PERSIST_H */
