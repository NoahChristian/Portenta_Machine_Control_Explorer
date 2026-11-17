/*
 * The two TCA6424As. Design and the warning about the maps are in expander.h.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "expander.h"

LOG_MODULE_REGISTER(pmc_io, LOG_LEVEL_INF);

#define DIN_NODE  DT_NODELABEL(pmc_din_expander)
#define PDIO_NODE DT_NODELABEL(pmc_pdio_expander)

static const struct device *const din_dev = DEVICE_DT_GET(DIN_NODE);
static const struct device *const pdio_dev = DEVICE_DT_GET(PDIO_NODE);

/*
 * A TCA6424A pin number is port * 8 + bit, so the mbed tables read directly:
 * P05 is port 0 bit 5, pin 5; P27 is port 2 bit 7, pin 23.
 *
 * DIN, channel -> pin:  P10 P05 P04 P02 P01 P00 P07 P06
 * Neither ascending nor a reversal, which is why it is a table.
 */
static const uint8_t din_pin[PMC_DIN_COUNT] = { 8, 5, 4, 2, 1, 0, 7, 6 };

/*
 * PDIO READ, channel -> pin: P27 P26 P25 P24 P23 P22 P21 P20 P17 P16 P15 P14.
 * Descending, and in a different port range from the write bits. A read-back
 * verify that reused the write map would confirm a write against the wrong
 * bit - which is the failure that looks exactly like success.
 */
static const uint8_t pdio_read_pin[PMC_PDIO_COUNT] = {
	23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12
};

static bool s_ready;

static int config_inputs(const struct device *dev, const uint8_t *pins, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		const int rc = gpio_pin_configure(dev, pins[i], GPIO_INPUT);

		if (rc != 0) {
			return rc;
		}
	}
	return 0;
}

int pmc_io_init(void)
{
	int rc;

	if (!device_is_ready(din_dev)) {
		LOG_ERR("DIN expander not ready");
		return -ENODEV;
	}
	if (!device_is_ready(pdio_dev)) {
		LOG_ERR("PDIO expander not ready");
		return -ENODEV;
	}

	rc = config_inputs(din_dev, din_pin, PMC_DIN_COUNT);
	if (rc != 0) {
		LOG_ERR("DIN inputs: %d", rc);
		return rc;
	}

	/* Only the READ pins. The write bits are left alone so that turning a
	 * PDIO channel into an output later does not have to undo anything. */
	rc = config_inputs(pdio_dev, pdio_read_pin, PMC_PDIO_COUNT);
	if (rc != 0) {
		LOG_ERR("PDIO inputs: %d", rc);
		return rc;
	}

	s_ready = true;
	LOG_INF("expanders ready: %u DIN, %u PDIO (maps are [mbed], unverified)",
		PMC_DIN_COUNT, PMC_PDIO_COUNT);
	return 0;
}

int pmc_io_raw(bool din_expander, uint32_t *word)
{
	const struct device *dev = din_expander ? din_dev : pdio_dev;
	gpio_port_value_t v = 0;
	int rc;

	if (!device_is_ready(dev)) {
		return -ENODEV;
	}
	rc = gpio_port_get_raw(dev, &v);
	if (rc == 0) {
		*word = (uint32_t)v;
	}
	return rc;
}

static void spread(uint32_t word, const uint8_t *pins, size_t n, int8_t *out)
{
	for (size_t i = 0; i < n; i++) {
		out[i] = (int8_t)((word >> pins[i]) & 1U);
	}
}

void pmc_io_read(struct pmc_io_state *out)
{
	uint32_t word;
	int rc;

	memset(out, 0, sizeof(*out));
	memset(out->din, -1, sizeof(out->din));
	memset(out->pdio, -1, sizeof(out->pdio));

	if (!s_ready) {
		out->din_err = -EAGAIN;
		out->pdio_err = -EAGAIN;
		return;
	}

	/*
	 * ONE PORT READ PER EXPANDER, not twenty pin reads. gpio_port_get_raw
	 * fetches all 24 bits in a single I2C transaction, so every channel in
	 * the snapshot is sampled at the same instant - which is the property
	 * the whole page is built on.
	 */
	rc = pmc_io_raw(true, &word);
	if (rc == 0) {
		spread(word, din_pin, PMC_DIN_COUNT, out->din);
		out->din_ok = true;
	} else {
		out->din_err = rc;
	}

	rc = pmc_io_raw(false, &word);
	if (rc == 0) {
		spread(word, pdio_read_pin, PMC_PDIO_COUNT, out->pdio);
		out->pdio_ok = true;
	} else {
		out->pdio_err = rc;
	}
}

/* ------------------------------------------------------- the 24 V rail --- */

#define ENC_SPEC(n) GPIO_DT_SPEC_GET(DT_NODELABEL(n), gpios)

static const struct gpio_dt_spec enc[] = {
	ENC_SPEC(enc0a), ENC_SPEC(enc0b), ENC_SPEC(enc0i),
	ENC_SPEC(enc1a), ENC_SPEC(enc1b), ENC_SPEC(enc1i),
};

void pmc_rail_read(struct pmc_rail *out)
{
	unsigned int high = 0;
	unsigned int read = 0;

	memset(out, 0, sizeof(*out));

	for (size_t i = 0; i < ARRAY_SIZE(enc); i++) {
		int v;

		if (!gpio_is_ready_dt(&enc[i])) {
			continue;
		}
		if (gpio_pin_configure_dt(&enc[i], GPIO_INPUT) != 0) {
			continue;
		}
		v = gpio_pin_get_dt(&enc[i]);
		if (v < 0) {
			continue;
		}
		read++;
		high += (unsigned int)(v != 0);
	}

	if (read != ARRAY_SIZE(enc)) {
		return;  /* sensed stays false: say nothing rather than guess */
	}

	out->sensed = true;
	out->highs = (uint8_t)high;
	out->present = (high == ARRAY_SIZE(enc));
	out->wired = (high != 0U && high != ARRAY_SIZE(enc));
}
