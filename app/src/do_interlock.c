/*
 * Interlock (0): every digital output starts de-energized. And the DO walk.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 *
 * THIS FILE USED TO DRIVE DO1 AS A BOOT INDICATOR - steady on while running
 * but not yet healthy, blinking once every health check passed, dark when the
 * firmware was not running at all. It was the only feedback this board gives
 * with no network and no console, and it has been REMOVED on purpose.
 *
 * The reason is that outputs are now remembered across a power cycle
 * (persist.h). An indicator the firmware strobes cannot also be a channel a
 * person sets and expects to find where they left it - DO1 would have been
 * the one output that silently refused to keep its state. Two owners, one
 * 24 V high-side switch. The channel went back to the person.
 *
 * What was given up, written down so nobody has to rediscover it: after a
 * reset there is now NOTHING on this board that distinguishes "running but
 * not yet healthy" from "in the DFU bootloader" from "crashed before
 * POST_KERNEL" from "unpowered", unless the network is up and the panel can
 * be reached. That distinction was the whole point of the blink - a
 * bootloader cannot fake a heartbeat, and a level cannot tell the three
 * failures apart. If it is ever wanted back, it is a k_work_delayable
 * toggling one channel, and the channel it picks must be taken out of the
 * remembered set.
 *
 * WHAT IS LEFT HERE IS THE PART THAT MUST NOT GO.
 *
 * Interlock (0) from docs/SAFETY.md: every one of the eight digital outputs
 * is configured and driven LOW at POST_KERNEL, before networking and before
 * anything can command one. An OTA revert can install an image whose idea of
 * the output states differs, and a board wired to real equipment must not
 * inherit whatever the previous image left energized. Restoring remembered
 * outputs does NOT weaken this: the restore happens later, from the health
 * thread, only once every check has passed - so a crash loop or a reverting
 * image never re-energizes anything. Everything low, then prove it, then put
 * back what was set.
 *
 * And the `dowalk` command, which lights each channel in turn so the DO map
 * can be confirmed at the connector. That map is [LIB] - inferred from the
 * mbed library's pins_mc.h and never checked against this hardware - and it
 * is exactly the class of error docs/MEASURED.md exists to retire. These are
 * 24 V high-side switches (2x TPS4H160AQPWPRQ1), so nothing is visible unless
 * 24 V is present on J6 pin 1.
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <stdlib.h>

#include "do_interlock.h"

LOG_MODULE_REGISTER(pmc_do_interlock, LOG_LEVEL_INF);

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
 * All eight digital outputs: this file drives every one of them low, and
 * `dowalk` lights each in turn.
 *
 * The connector is labeled 00..07 and the mbed library calls them DO0..DO7, so
 * a bare "D1" is ambiguous between the first and second channel depending on
 * whether you are counting the silkscreen or the terminals. The walk settles
 * it by watching which LED moves, which is a measurement rather than a reading
 * of somebody's header file.
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

static const struct device *const latch_dev = DEVICE_DT_GET(LATCH_PORT);

static bool s_ready;

/*
 * The TPS4H160's outputs are latched: the datasheet's own words for the
 * programmable channels are that the latch "must be toggled to activate the
 * channel again". Pulsing it after a level change is therefore part of
 * writing an output, not an optional extra - and getting this wrong looks
 * exactly like a dead channel.
 *
 * UNVERIFIED, like the pin mapping. If a channel does not follow, the latch is
 * the first thing to suspect: it may need a different edge, a different width,
 * or may not gate this bank at all.
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

static int pmc_do_interlock_init(void)
{
	if (!device_is_ready(latch_dev)) {
		LOG_ERR("output latch port not ready; outputs NOT interlocked");
		return -ENODEV;
	}

	/*
	 * Every channel is configured and driven LOW.
	 *
	 * This is interlock (0) from docs/SAFETY.md doing its job on the very
	 * first boot: an OTA revert can install an image whose idea of the
	 * output states differs, and a board wired to real equipment must not
	 * inherit whatever the previous image left energized. De-energizing
	 * all eight before anything else is the only safe starting point.
	 *
	 * REMEMBERED OUTPUTS DO NOT WEAKEN THIS. persist.c puts them back from
	 * the health thread, after every check has passed - never here. The
	 * order is: everything low, then prove it, then restore.
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
	latch_pulse();
	LOG_INF("all 8 digital outputs de-energized and latched (interlock 0)");
	return 0;
}

/*
 * After the GPIO driver, before the application.
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
SYS_INIT(pmc_do_interlock_init, POST_KERNEL, 45);

/*
 * `dowalk` - light each digital output in turn so the map can be CONFIRMED.
 *
 * The one thing the removed indicator was also good for, kept because it is
 * the measurement that retires risk 5: the DO pin table is [LIB], read out of
 * another program's header, and the only way to know which terminal a channel
 * actually reaches is to watch one move.
 *
 * IT DRIVES 24 V AND IT LEAVES EVERYTHING OFF. One channel at a time, never
 * two - the point is to see which LED moves, which needs exactly one moving -
 * and every channel is put back down at the end regardless of how it exits.
 *
 * It deliberately does NOT go through pmc_io_write(), so it writes nothing to
 * the remembered set: a bring-up sweep is not a state anybody chose to keep,
 * and finding all eight outputs restored at the next power-up because of a
 * diagnostic would be a nasty surprise.
 */
static int cmd_dowalk(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!s_ready) {
		shell_error(sh, "outputs are not configured");
		return -ENODEV;
	}

	shell_print(sh, "one channel at a time, 1 s each. 24 V must be on J6");
	shell_print(sh, "pin 1 for anything to be visible.");

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

	shell_print(sh, "");
	shell_print(sh, "all channels off. The labels are J6's own silkscreen,");
	shell_print(sh, "00..07. If the LED that moved is not the one you expect,");
	shell_print(sh, "the pin table in do_interlock.c is wrong - it is inferred");
	shell_print(sh, "from the mbed library and has never been checked against");
	shell_print(sh, "this hardware. Record what you see in docs/MEASURED.md.");
	shell_print(sh, "");
	shell_print(sh, "NOTE: this bypasses the remembered-output set, so nothing");
	shell_print(sh, "here is stored or comes back at the next power-up.");
	return 0;
}

SHELL_CMD_ARG_REGISTER(dowalk, NULL,
		       "Light each digital output in turn, 1 s each, to confirm "
		       "the channel map at the connector.",
		       cmd_dowalk, 1, 0);
