/*
 * Driving DO and PDIO. The three-way verdict and why DO cannot be verified
 * are in io_write.h.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include "expander.h"
#include "io_write.h"

LOG_MODULE_REGISTER(pmc_wr, LOG_LEVEL_INF);

/* ------------------------------------------------------------- tables --- */

/*
 * DO, channel -> H7 pin. [mbed] from pins_mc.h, with DO1 confirmed on this
 * board: its LED is the one above the terminal silkscreened 01, so the
 * DO0..DO7 / 00..07 correspondence is 1:1 and the rest follow - the wiring is
 * PCB traces, not a software choice.
 */
struct do_pin {
	const struct device *port;
	gpio_pin_t pin;
};

#define DO_PIN(node, p) { .port = DEVICE_DT_GET(DT_NODELABEL(node)), .pin = (p) }

static const struct do_pin do_pins[8] = {
	DO_PIN(gpioi, 6),  DO_PIN(gpioh, 9),  DO_PIN(gpioj, 9),  DO_PIN(gpioe, 2),
	DO_PIN(gpioi, 3),  DO_PIN(gpioi, 2),  DO_PIN(gpiod, 3),  DO_PIN(gpioa, 14),
};

/* MC_DO_LATCH_PIN PB_2 and MC_PDIO_LATCH_PIN PH_14. */
static const struct device *const do_latch = DEVICE_DT_GET(DT_NODELABEL(gpiob));
#define DO_LATCH_PIN 2
static const struct device *const pdio_latch = DEVICE_DT_GET(DT_NODELABEL(gpioh));
#define PDIO_LATCH_PIN 14

/*
 * PDIO WRITE bits - note 4..7 are reversed inside the nibble, which is why
 * this is a table and not arithmetic. The READ map lives in expander.c and is
 * deliberately not reachable from here: the verify must not be able to reuse
 * the write mapping by accident.
 */
static const uint8_t pdio_write_pin[PMC_PDIO_COUNT] = {
	0, 1, 2, 3, 7, 6, 5, 4, 8, 9, 10, 11
};

#define PDIO_NODE DT_NODELABEL(pmc_pdio_expander)
static const struct device *const pdio_dev = DEVICE_DT_GET(PDIO_NODE);

static bool s_ready;

const char *pmc_write_result_str(enum pmc_write_result r)
{
	switch (r) {
	case PMC_W_OK:        return "verified";
	case PMC_W_COMMANDED: return "commanded";
	case PMC_W_NO_SUPPLY: return "no supply";
	case PMC_W_MISMATCH:  return "MISMATCH";
	case PMC_W_REFUSED:   return "refused";
	default:              return "error";
	}
}

/* --------------------------------------------------------------- init --- */

int pmc_io_write_init(void)
{
	int rc;

	for (size_t i = 0; i < ARRAY_SIZE(do_pins); i++) {
		if (!device_is_ready(do_pins[i].port)) {
			LOG_ERR("DO%u port not ready", (unsigned)i);
			return -ENODEV;
		}
		rc = gpio_pin_configure(do_pins[i].port, do_pins[i].pin,
					GPIO_OUTPUT_INACTIVE);
		if (rc != 0) {
			LOG_ERR("DO%u configure: %d", (unsigned)i, rc);
			return rc;
		}
	}

	if (!device_is_ready(do_latch) || !device_is_ready(pdio_latch)) {
		return -ENODEV;
	}
	rc = gpio_pin_configure(do_latch, DO_LATCH_PIN, GPIO_OUTPUT_INACTIVE);
	if (rc != 0) {
		return rc;
	}
	rc = gpio_pin_configure(pdio_latch, PDIO_LATCH_PIN, GPIO_OUTPUT_INACTIVE);
	if (rc != 0) {
		return rc;
	}

	s_ready = true;
	LOG_INF("outputs ready: 8 DO (unverifiable), %u PDIO (read-back)",
		PMC_PDIO_COUNT);
	return 0;
}

/*
 * The latch. The datasheet's only word on it is that a channel shut down by
 * the current limit needs the enable toggled to come back, so the pulse width
 * is a guess with a margin rather than a figure from anywhere.
 */
static void pulse_latch(const struct device *dev, gpio_pin_t pin)
{
	gpio_pin_set(dev, pin, 1);
	k_busy_wait(50);
	gpio_pin_set(dev, pin, 0);
}

/* -------------------------------------------------------------- reads --- */

int pmc_do_read(int8_t *out, size_t n)
{
	int rc = 0;

	for (size_t i = 0; i < n; i++) {
		int v;

		if (i >= ARRAY_SIZE(do_pins) ||
		    !device_is_ready(do_pins[i].port)) {
			out[i] = -1;
			rc = -ENODEV;
			continue;
		}

		v = gpio_pin_get(do_pins[i].port, do_pins[i].pin);
		if (v < 0) {
			out[i] = -1;
			rc = v;
			continue;
		}
		out[i] = (int8_t)(v ? 1 : 0);
	}

	return rc;
}

/* ------------------------------------------------------------- writes --- */

static enum pmc_write_result write_do(int n, bool on, char *detail, size_t cap)
{
	int rc;

	if (n < 0 || n >= (int)ARRAY_SIZE(do_pins)) {
		(void)snprintk(detail, cap, "DO%d is not a channel", n);
		return PMC_W_REFUSED;
	}

	rc = gpio_pin_set(do_pins[n].port, do_pins[n].pin, on ? 1 : 0);
	if (rc != 0) {
		(void)snprintk(detail, cap, "gpio_pin_set failed (%d)", rc);
		return PMC_W_ERROR;
	}
	pulse_latch(do_latch, DO_LATCH_PIN);

	/*
	 * The output is on, and the panel says so without hedging. Whether
	 * 24 V reaches the terminal is the wiring's business; the controller's
	 * claim is that it drove the channel, and it knows that exactly.
	 */
	(void)snprintk(detail, cap, "DO%d is now %s", n, on ? "ON" : "OFF");
	return PMC_W_COMMANDED;
}

static enum pmc_write_result write_pdio(int n, bool on, char *detail, size_t cap)
{
	struct pmc_io_state st;
	int rc;

	if (n < 0 || n >= PMC_PDIO_COUNT) {
		(void)snprintk(detail, cap, "PDIO%d is not a channel", n);
		return PMC_W_REFUSED;
	}

	rc = gpio_pin_configure(pdio_dev, pdio_write_pin[n], GPIO_OUTPUT);
	if (rc != 0) {
		(void)snprintk(detail, cap, "configure failed (%d)", rc);
		return PMC_W_ERROR;
	}
	rc = gpio_pin_set(pdio_dev, pdio_write_pin[n], on ? 1 : 0);
	if (rc != 0) {
		(void)snprintk(detail, cap, "set failed (%d)", rc);
		return PMC_W_ERROR;
	}
	pulse_latch(pdio_latch, PDIO_LATCH_PIN);

	/* Let the high-side switch and the load settle before believing a
	 * read. 2 ms is arbitrary and generous; nothing documents it. */
	k_msleep(2);

	pmc_io_read(&st);
	if (!st.pdio_ok) {
		(void)snprintk(detail, cap, "expander did not answer (%d)",
			       st.pdio_err);
		return PMC_W_ERROR;
	}

	if (st.pdio[n] == (on ? 1 : 0)) {
		(void)snprintk(detail, cap, "PDIO%d read back %s", n,
			       on ? "high" : "low");
		return PMC_W_OK;
	}

	/*
	 * Commanded high and reading low is the expected result when J8's
	 * 24V IN is unfed - there is no rail for the switch to pass. Calling
	 * that a mismatch would send someone looking for a broken board
	 * instead of a missing wire, so it is named for what it is.
	 */
	if (on && st.pdio[n] == 0) {
		(void)snprintk(detail, cap,
			       "PDIO%d commanded high, read low - J8 24V IN is "
			       "unfed, so there is no rail to switch", n);
		return PMC_W_NO_SUPPLY;
	}

	(void)snprintk(detail, cap, "PDIO%d commanded %s but read %s", n,
		       on ? "high" : "low", st.pdio[n] ? "high" : "low");
	return PMC_W_MISMATCH;
}

enum pmc_write_result pmc_io_write(const char *chan, bool on,
				   char *detail, size_t cap)
{
	if (!s_ready) {
		(void)snprintk(detail, cap, "outputs not initialized");
		return PMC_W_ERROR;
	}
	if (chan == NULL) {
		(void)snprintk(detail, cap, "no channel named");
		return PMC_W_REFUSED;
	}

	if (strncmp(chan, "PDIO", 4) == 0) {
		return write_pdio(atoi(chan + 4), on, detail, cap);
	}
	if (strncmp(chan, "DO", 2) == 0) {
		return write_do(atoi(chan + 2), on, detail, cap);
	}

	(void)snprintk(detail, cap, "%s is not a writable channel", chan);
	return PMC_W_REFUSED;
}

enum pmc_write_result pmc_io_pulse(const char *chan, uint32_t ms,
				   char *detail, size_t cap)
{
	enum pmc_write_result r = pmc_io_write(chan, true, detail, cap);

	if (r == PMC_W_REFUSED || r == PMC_W_ERROR) {
		return r;
	}

	k_msleep(ms);
	(void)pmc_io_write(chan, false, detail, cap);

	/* The verdict that matters is the one from driving it high; the
	 * trailing low is housekeeping. Restate it rather than let the second
	 * call's text stand. */
	(void)snprintk(detail, cap, "%s pulsed high for %u ms (%s)", chan,
		       (unsigned)ms, pmc_write_result_str(r));
	return r;
}
