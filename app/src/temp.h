/*
 * Temperature: three channels, two converters, one bus.
 */
#ifndef PMC_TEMP_H_
#define PMC_TEMP_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PMC_TEMP_COUNT 3

/*
 * The acquisition mode. It is a property of the BOARD, not of a channel:
 * datasheet 3-9 gives exactly one single-pole double-throw switch choosing
 * between the two converters, so whichever way it is thrown applies to
 * whichever channel is currently selected. Reading one channel as PT100 and
 * another as a thermocouple in the same sweep would mean throwing it twice
 * per sweep; that is possible and is not what this does, because a mode the
 * whole board shares is a mode a person can see on one control.
 */
enum pmc_temp_mode {
	PMC_TEMP_PT100 = 0,  /* MAX31865 */
	PMC_TEMP_J     = 1,  /* MAX31855K + a coefficient; see temp.c */
	PMC_TEMP_K     = 2,  /* MAX31855K, the part's own type */
};

enum pmc_temp_state {
	PMC_T_UNREAD = 0,  /* no sweep has reached this channel yet */
	PMC_T_OK,
	PMC_T_OPEN,        /* no probe, or a broken lead */
	PMC_T_FAULT,       /* the converter raised a fault bit */
	PMC_T_ERR,         /* the bus transaction itself failed */
};

struct pmc_temp_chan {
	enum pmc_temp_state state;
	int32_t milli_c;     /* the reported temperature, thousandths */
	int32_t cold_milli_c;/* cold junction; thermocouple modes only */
	int32_t raw_milli_c; /* what the converter said before any coefficient */

	/*
	 * THE CONVERTER'S OWN WORD, VERBATIM. Thermocouple modes only.
	 *
	 * Without this, "open" is unfalsifiable. Zephyr's MAX31855 driver
	 * checks the fault bit BEFORE the channel switch, so a faulted read
	 * returns -EIO for the die temperature too - and the die temperature
	 * is the only field that is valid whether or not a probe is attached.
	 * Three channels reporting "open" is then equally consistent with
	 * three empty terminal blocks and with a chip that is not answering at
	 * all, because an undriven MISO reads as all ones and all ones has the
	 * fault bit set.
	 *
	 * So the raw word is captured separately and rendered. 0xFFFFFFFF is a
	 * floating bus; 0x00000000 is a bus stuck low; anything else carries a
	 * die temperature in bits 4..15 that should look like a warm room.
	 */
	uint32_t raw_word;

	/*
	 * The MAX31865's fault status register, PT100 mode only, and the
	 * reason this family is worth more than the thermocouple side: where
	 * the MAX31855 gives one open-circuit bit, this names which wire.
	 * 0 means no fault was read.
	 */
	uint8_t rtd_fault;
};

/* A human cause for a fault byte, or NULL if it carries none. */
const char *pmc_temp_rtd_fault_str(uint8_t f);

/* 2 for two/four-wire, 3 for three-wire. */
uint8_t pmc_temp_wires_get(void);
bool pmc_temp_wires_set(uint8_t wires, char *detail, size_t cap);

int pmc_temp_init(void);

/* Snapshot of one channel. Returns false if n is not a channel. */
bool pmc_temp_get(uint8_t n, struct pmc_temp_chan *out);

enum pmc_temp_mode pmc_temp_mode_get(void);
const char *pmc_temp_mode_name(enum pmc_temp_mode m);

/*
 * Set the mode. Takes effect on the next sweep, and every channel is marked
 * unread immediately - a reading taken through the other converter is not a
 * reading in this mode, and leaving the old numbers on screen under a new
 * label is the exact lie this family makes easy.
 */
bool pmc_temp_mode_set(const char *name, char *detail, size_t cap);

/* True once at least one sweep has completed. */
bool pmc_temp_ready(void);

/* How many sweeps have completed, and how many reads faulted. */
uint32_t pmc_temp_sweeps(void);

/* How many times the mode has been set since boot. An instrument, not a
 * feature - see temp.c. */
uint32_t pmc_temp_mode_sets(void);
uint32_t pmc_temp_faults(void);

#ifdef __cplusplus
}
#endif

#endif /* PMC_TEMP_H_ */
