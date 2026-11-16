/*
 * The one piece of user feedback this board has.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Written by Noah Christian (C) 2026
 *
 * The Portenta Machine Control has NO display and no software-controllable
 * LED - the only LED the datasheet mentions is a hard-wired power indicator.
 * So after pressing RESET there is nothing at all to tell you whether the
 * board came back, whether it came back running the image you just flashed, or
 * whether it is sitting in a bootloader.
 *
 * This drives one digital output as a boot indicator:
 *
 *     HIGH  from earliest boot: the firmware is running but NOT yet healthy
 *     LOW   every health check has passed
 *
 * so "D1 went low" is a positive statement that this image came up and its I/O
 * answered, and "D1 stayed high" distinguishes a board that is running badly
 * from one that is not running at all - which a dark board does not.
 *
 * THE POLARITY IS DELIBERATE AND IT IS THE SAFE WAY ROUND. The output is
 * de-energized in the good state, so the indicator costs nothing in steady
 * operation, and a board that is unpowered, held in reset, stuck in a
 * bootloader, or crashed before this code runs reads the SAME as a board that
 * is merely unhealthy. That is honest: none of those states has proved
 * anything about the application.
 *
 * WHAT THIS DRIVES, AND THE CAVEAT. DO1 is `MC_DO_DO1_PIN PH_9` in the mbed
 * library's pins_mc.h, which is J6 pin 3 - channel 01 of the digital outputs.
 * Those are 24 V high-side switches (2x TPS4H160AQPWPRQ1), so:
 *
 *   - it does nothing visible unless 24 V is present on J6 pin 1, and
 *   - the pin mapping is [LIB], inferred from another program's source and NOT
 *     yet confirmed on this hardware.
 *
 * If the wrong channel energizes, that mapping is the thing that is wrong, and
 * it is exactly the class of error docs/MEASURED.md exists to retire. Confirm
 * at the connector before trusting it.
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <stdlib.h>

#include "status_out.h"

LOG_MODULE_REGISTER(pmc_status, LOG_LEVEL_INF);

/*
 * DO1 and the shared output latch, from pins_mc.h:
 *
 *     MC_DO_DO1_PIN    PH_9
 *     MC_DO_LATCH_PIN  PB_2
 *
 * Written as raw port/pin rather than through a devicetree alias on purpose,
 * for now: the channel table that will own every one of these is generated
 * from tools/channels.csv and does not exist yet, and inventing a one-off
 * alias here would create a second place where a pin number lives. There must
 * be exactly one. This comment is the marker for moving it.
 */
#define LATCH_PORT DT_NODELABEL(gpiob)
#define LATCH_PIN  2

/*
 * All eight digital outputs, so the indicator channel can be MOVED at runtime.
 *
 * The connector is labeled 00..07 and the mbed library calls them DO0..DO7, so
 * "D1" is ambiguous between the first and second channel depending on whether
 * you are counting the silkscreen or the terminals. Rather than guess, `d1 ch
 * <n>` re-points the indicator and `d1 walk` lights each in turn - so the
 * question is settled by watching which LED moves, which is a measurement
 * rather than a reading of somebody's header file.
 *
 * Pin assignments are [LIB], from pins_mc.h, and unconfirmed. This table is
 * the embryo of the generated channel table; when tools/channels.csv exists
 * this goes away and the names come from there.
 */
struct do_chan {
	const char *label;             /* as silkscreened on J6 */
	const struct device *port;
	gpio_pin_t pin;
	const char *pad;
};

#define DO_ENTRY(lbl, node, p, padname) 	{ .label = lbl, .port = DEVICE_DT_GET(DT_NODELABEL(node)), 	  .pin = (p), .pad = padname }

static const struct do_chan do_chans[] = {
	DO_ENTRY("00", gpioi, 6,  "PI_6"),
	DO_ENTRY("01", gpioh, 9,  "PH_9"),
	DO_ENTRY("02", gpioj, 9,  "PJ_9"),
	DO_ENTRY("03", gpioe, 2,  "PE_2"),
	DO_ENTRY("04", gpioi, 3,  "PI_3"),
	DO_ENTRY("05", gpioi, 2,  "PI_2"),
	DO_ENTRY("06", gpiod, 3,  "PD_3"),
	DO_ENTRY("07", gpioa, 14, "PA_14"),
};

/*
 * Which channel is the indicator. Defaults to 01, which is what "D1" most
 * likely means given the connector's own 00..07 labeling - but see above: this
 * is a guess until an LED has been watched.
 */
static uint8_t s_indicator = 1;

static const struct device *const latch_dev = DEVICE_DT_GET(LATCH_PORT);

static bool s_ready;

/*
 * The TPS4H160's outputs are latched: the datasheet's own words for the
 * programmable channels are that the latch "must be toggled to activate the
 * channel again". Pulsing it after a level change is therefore part of
 * writing an output, not an optional extra - and getting this wrong looks
 * exactly like a dead channel.
 *
 * UNVERIFIED, like the pin mapping. If DO1 does not follow, the latch is the
 * first thing to suspect: it may need a different edge, a different width, or
 * may not gate this bank at all.
 */
static void latch_pulse(void)
{
	if (!s_ready) {
		return;
	}
	gpio_pin_set(latch_dev, LATCH_PIN, 1);
	k_busy_wait(10);
	gpio_pin_set(latch_dev, LATCH_PIN, 0);
}

void pmc_status_set(bool busy)
{
	if (!s_ready) {
		return;
	}
	gpio_pin_set(do_chans[s_indicator].port, do_chans[s_indicator].pin,
		     busy ? 1 : 0);
	latch_pulse();
}

static int pmc_status_init(void)
{
	if (!device_is_ready(latch_dev)) {
		LOG_ERR("output latch port not ready; no boot indicator");
		return -ENODEV;
	}

	/*
	 * Every channel is configured and driven LOW, not just the indicator.
	 *
	 * This is interlock (0) from docs/SAFETY.md doing its job on the very
	 * first boot: an OTA revert can install an image whose idea of the
	 * output states differs, and a board wired to real equipment must not
	 * inherit whatever the previous image left energized. De-energizing
	 * all eight before anything else is the only safe starting point.
	 */
	int rc = 0;

	for (size_t i = 0; i < ARRAY_SIZE(do_chans); i++) {
		if (!device_is_ready(do_chans[i].port)) {
			LOG_ERR("GPIO port for DO%s not ready", do_chans[i].label);
			return -ENODEV;
		}
		rc = gpio_pin_configure(do_chans[i].port, do_chans[i].pin,
					GPIO_OUTPUT_INACTIVE);
		if (rc != 0) {
			LOG_ERR("DO%s configure failed (%d)", do_chans[i].label, rc);
			return rc;
		}
	}

	rc = gpio_pin_configure(latch_dev, LATCH_PIN, GPIO_OUTPUT_INACTIVE);
	if (rc != 0) {
		LOG_ERR("output latch configure failed (%d)", rc);
		return rc;
	}

	s_ready = true;

	/*
	 * Asserted here, as early as the GPIO driver allows, so the window in
	 * which a running-but-unhealthy board looks identical to a dead one is
	 * as small as it can be.
	 */
	pmc_status_set(true);
	LOG_INF("DO1 high: booting. It goes low when every health check passes.");
	return 0;
}

/*
 * After the GPIO driver, before the application. The health thread that clears
 * it does not start for another second.
 *
 * THE PRIORITY IS A LITERAL, and it has to be. Zephyr builds its init levels
 * with linker sections named from this token, so an expression like
 * CONFIG_GPIO_INIT_PRIORITY + 1 does not evaluate - it produces a section name
 * nothing defines, and the only diagnostic is
 *
 *     ld.bfd.exe: Undefined initialization levels used.
 *
 * which names neither the file nor the symbol. 45 is after the GPIO driver's
 * default of 40.
 */
SYS_INIT(pmc_status_init, POST_KERNEL, 45);

static void d1_report(const struct shell *sh)
{
	const struct do_chan *c = &do_chans[s_indicator];

	shell_print(sh, "indicator is channel %s (%s, J6 pin %d): %s",
		    c->label, c->pad, s_indicator + 2,
		    gpio_pin_get(c->port, c->pin) ? "HIGH - booting or unhealthy"
						  : "low - healthy");
}

static int cmd_d1(const struct shell *sh, size_t argc, char **argv)
{
	if (!s_ready) {
		shell_error(sh, "outputs are not configured");
		return -ENODEV;
	}

	if (argc == 1) {
		d1_report(sh);
		shell_print(sh, "");
		shell_print(sh, "  d1 high | low     drive it by hand");
		shell_print(sh, "  d1 ch <0-7>       move the indicator to another channel");
		shell_print(sh, "  d1 walk           light each channel in turn, 1 s each");
		shell_print(sh, "");
		shell_print(sh, "The channel labels are J6's own silkscreen, 00..07. If the");
		shell_print(sh, "LED that moves is not the one you expect, the pin table in");
		shell_print(sh, "status_out.c is wrong - it is inferred from the mbed library");
		shell_print(sh, "and has never been checked against this hardware.");
		return 0;
	}

	if (strcmp(argv[1], "high") == 0) {
		pmc_status_set(true);
	} else if (strcmp(argv[1], "low") == 0) {
		pmc_status_set(false);
	} else if (strcmp(argv[1], "ch") == 0 && argc == 3) {
		const int n = atoi(argv[2]);

		if (n < 0 || n >= (int)ARRAY_SIZE(do_chans)) {
			shell_error(sh, "channel must be 0..%d", (int)ARRAY_SIZE(do_chans) - 1);
			return -EINVAL;
		}
		/* Put the old one back down before moving, or two stay lit. */
		pmc_status_set(false);
		s_indicator = (uint8_t)n;
		pmc_status_set(true);
	} else if (strcmp(argv[1], "walk") == 0) {
		/*
		 * One at a time, never two: this drives 24 V high-side
		 * switches, and the point is to see which LED moves, which
		 * needs exactly one moving.
		 */
		for (size_t i = 0; i < ARRAY_SIZE(do_chans); i++) {
			for (size_t j = 0; j < ARRAY_SIZE(do_chans); j++) {
				gpio_pin_set(do_chans[j].port, do_chans[j].pin, i == j);
			}
			latch_pulse();
			shell_print(sh, "  channel %s  (%s, J6 pin %d)",
				    do_chans[i].label, do_chans[i].pad, (int)i + 2);
			k_sleep(K_MSEC(1000));
		}
		for (size_t j = 0; j < ARRAY_SIZE(do_chans); j++) {
			gpio_pin_set(do_chans[j].port, do_chans[j].pin, 0);
		}
		latch_pulse();
		shell_print(sh, "all channels off; indicator restored");
		pmc_status_set(!pmc_health_is_healthy());
		return 0;
	} else {
		shell_print(sh, "usage: d1 [high|low|ch <0-7>|walk]");
		return -EINVAL;
	}

	d1_report(sh);
	return 0;
}

SHELL_CMD_ARG_REGISTER(d1, NULL,
		       "Boot indicator on a digital output (d1 [high|low|ch <n>|walk]).",
		       cmd_d1, 1, 2);
