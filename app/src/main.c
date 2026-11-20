/*
 * Portenta Machine Control Explorer.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 *
 * M0 STATE. This does nothing with the carrier's I/O yet, on purpose. M0
 * exists to produce two measurements, and both of them constrain decisions
 * that are expensive to change afterwards:
 *
 *   1. The size of an image with WiFi compiled in. The AIROC 4343WA1 firmware
 *      is 421,421 bytes and there is no Kconfig to load it from the QSPI
 *      airoc_firmware partition, so it is linked INTO the image. That single
 *      number decides whether two internal MCUboot slots are possible at all.
 *      They are not: see docs/FLASH.md.
 *
 *   2. What the three analog inputs actually read. The STM32H747's SYSCFG
 *      analog switches are CLOSED after reset, and on this board that ties
 *      PA_1C/PC_2C/PC_3C - the carrier's three analog inputs - to the Ethernet
 *      RMII clock and to SPI2. Nothing in Zephyr opens them. See
 *      boards/shields/.../pmc_syscfg_init.c.
 *
 * The board has NO display of any kind. The USB CDC-ACM shell and, later, the
 * web panel are the only two ways to see anything, which is why the shell
 * commands here are not a convenience.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/drivers/flash.h>

#include "clock.h"
#include "expander.h"
#include "ao.h"
#include "temp.h"
#include "rs485.h"
#include "io_write.h"
#include "persist.h"
#include "whoami.h"
#include "enc.h"
#include "canbus.h"
#include "canopen.h"
#include "page_vars.h"


LOG_MODULE_REGISTER(pmc, LOG_LEVEL_INF);

/*
 * Entropy, asserted rather than assumed.
 *
 * CONFIG_CSPRNG_NEEDED is a request; CONFIG_CSPRNG_ENABLED is the answer, and
 * the answer can be no even with an entropy node in the devicetree, because
 * the driver may be gated by some other Kconfig that is not set. Zephyr's own
 * help text says to enable the first and then check the second.
 *
 * It matters here more than it looks. The web panel's form layer refuses EVERY
 * write when its CSRF token has no entropy behind it - deliberately failing
 * closed rather than seeding from uptime, which would produce a token that
 * looks exactly right and is guessable. A silent "no" here therefore surfaces
 * much later as a panel whose buttons all appear to do nothing, with nothing
 * in any log to say why. Failing the BUILD is cheaper.
 */
BUILD_ASSERT(IS_ENABLED(CONFIG_CSPRNG_ENABLED),
	     "no cryptographically secure entropy source was pulled into this "
	     "build; the panel would refuse every write at runtime");

/*
 * Every partition this application believes in, by nodelabel.
 *
 * Printed rather than assumed. The flash map is the one thing MCUboot and this
 * application MUST agree about, and when they disagree nothing fails loudly:
 * MCUboot writes, validates and swaps at addresses the application does not
 * share, and the symptom is a corrupt image or a silently ignored update
 * rather than a build error. Both sides include pmc-partitions.dtsi, and this
 * command is how you check that the include actually took effect - in
 * particular that the /delete-node/ of the board's own internal slot1 and
 * scratch resolved, instead of quietly leaving the old nodes in place.
 */
struct part_row {
	const char *name;
	uint8_t     id;
};

static int cmd_pmc_parts(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	static const struct part_row rows[] = {
#if PARTITION_EXISTS(boot_partition)
		{ "boot_partition  (mcuboot)", PARTITION_ID(boot_partition) },
#endif
#if PARTITION_EXISTS(slot0_partition)
		{ "slot0_partition (image-0)", PARTITION_ID(slot0_partition) },
#endif
#if PARTITION_EXISTS(slot1_partition)
		{ "slot1_partition (image-1)", PARTITION_ID(slot1_partition) },
#endif
#if PARTITION_EXISTS(scratch_partition)
		{ "scratch_partition", PARTITION_ID(scratch_partition) },
#endif
#if PARTITION_EXISTS(web_partition)
		{ "web_partition (LittleFS)", PARTITION_ID(web_partition) },
#endif
#if PARTITION_EXISTS(kvs_partition)
		{ "kvs_partition (settings)", PARTITION_ID(kvs_partition) },
#endif
	};

	shell_print(sh, "%-28s %-10s %-10s %-9s %s",
		    "partition", "offset", "size", "sector", "device");

	for (size_t i = 0; i < ARRAY_SIZE(rows); i++) {
		const struct flash_area *fa;
		int rc = flash_area_open(rows[i].id, &fa);

		if (rc != 0) {
			shell_print(sh, "%-28s  open failed (%d)", rows[i].name, rc);
			continue;
		}

		/*
		 * The erase-block size is printed because it is the number the
		 * swap turns on. MCUboot's boot_slots_compatible() compares
		 * sector geometry between the slots, and the scratch area has
		 * to be STRICTLY larger than the largest sector it must hold -
		 * an exactly-equal scratch passes the compatibility check and
		 * then fails during the swap itself, because a scratch trailer
		 * is written into the same area. 128 KB internally against
		 * 4 KB in the QSPI NOR is exactly why this layout needs
		 * swap-using-scratch and cannot use swap-using-move.
		 */
		const struct device *dev = flash_area_get_device(fa);
		struct flash_pages_info pg;
		size_t sector = 0;

		if (dev != NULL && flash_get_page_info_by_offs(dev, fa->fa_off, &pg) == 0) {
			sector = pg.size;
		}

		shell_print(sh, "%-28s 0x%08lx 0x%08zx %-9zu %s",
			    rows[i].name,
			    (unsigned long)fa->fa_off,
			    (size_t)fa->fa_size,
			    sector,
			    dev != NULL ? dev->name : "(none)");

		flash_area_close(fa);
	}

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_pmc,
	SHELL_CMD(parts, NULL,
		  "Print every flash partition this image believes in.",
		  cmd_pmc_parts),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(pmc, &sub_pmc, "Portenta Machine Control Explorer", NULL);

int main(void)
{
	LOG_INF("Portenta Machine Control Explorer");
	LOG_INF("board %s", CONFIG_BOARD_TARGET);

	/*
	 * The panel is NOT started here. zsock_bind() returns ENOENT while no
	 * interface has an address, so a server started from main() binds
	 * nothing and never retries - see the L4_CONNECTED handler in net.c,
	 * which starts it when there is actually something to bind to.
	 *
	 * Its SELF-TEST is here, though, and that split is deliberate twice
	 * over. It renders the whole page to a counting sink and logs every
	 * tag that does not resolve against the registry in page_vars.cpp, so a
	 * generated header that no longer matches the registry is a line in the
	 * boot log rather than a hole somebody notices in a browser an hour
	 * later - and it runs now rather than on first request, because the
	 * point of a self-test is to have already run.
	 *
	 * AND IT HAS TO BE ON THIS STACK. HtmlTpl::render() puts its
	 * HTMLTPL_OUTBUF coalescing buffer on the caller's stack, which is 1460
	 * bytes; CONFIG_MAIN_STACK_SIZE is raised to 8192 to hold it. Calling
	 * it from the L4 handler instead put it on the 2048-byte system
	 * workqueue stack and faulted the board on its first boot.
	 */
	pmc_page_init();

	/*
	 * Started here rather than from the network event, because it owns a
	 * thread and a thread is created once, from a stack that is known. It
	 * blocks until an address exists, so starting it early costs nothing.
	 */
	pmc_clock_init();

	/* Configures the expander input pins. A failure here is reported and
	   not fatal: the rest of the panel still works without DIN. */
	(void)pmc_io_init();
	(void)pmc_io_write_init();
	(void)pmc_ao_init();
	/* Temperature starts its own sweep thread; a failure here is
	 * reported by the health check rather than stopping boot. */
	(void)pmc_temp_init();
	(void)pmc_rs485_init();
	/*
	 * Encoders arm six GPIO interrupts; CAN brings up FDCAN1 and drives
	 * the transceiver's standby pin. Both report their own failures and
	 * neither is allowed to stop the panel coming up.
	 *
	 * Each has a Kconfig that turns it off, and they are there to be used:
	 * these two arrived together and the radio stopped answering in the
	 * same build, which is a question one flash cycle settles and an
	 * argument does not.
	 */
	/* pmc_enc_init() honors CONFIG_PMC_ENCODER_CHANNELS itself, including
	   0, so there is no guard here - one place decides how many. */
	(void)pmc_enc_init();
	if (IS_ENABLED(CONFIG_PMC_CANBUS)) {
		(void)pmc_can_init();
		/*
		 * AFTER the controller, because it installs the receive tap
		 * and pmc_can_init() is what adds the filter that feeds it.
		 * It also starts transmitting a heartbeat, which on a wire
		 * with no second node leaves the controller retransmitting in
		 * ERROR_PASSIVE - that is what a lone CAN node does, it is
		 * reported rather than hidden, and CONFIG_PMC_CANOPEN turns
		 * it off.
		 */
		if (IS_ENABLED(CONFIG_PMC_CANOPEN)) {
			(void)pmc_canopen_init();
		}
	}

	/*
	 * LAST, AND THAT ORDER IS THE WHOLE POINT. pmc_persist_init() applies
	 * the stored RS485 line settings and probe type through the same
	 * setters a person would use, so the drivers it configures have to
	 * exist first. It does NOT touch an output - those go back from the
	 * health thread, once every check has passed (persist.h).
	 */
	(void)pmc_persist_init();

	/*
	 * AFTER pmc_persist_init(), because that is what runs
	 * settings_subsys_init() and the boot history lives in settings.
	 * Recording this boot before the subsystem exists would write nothing
	 * and leave a history claiming the board has never started.
	 *
	 * It also reads and CLEARS the reset cause, so this must be the only
	 * caller -- a second reader would find the register already wiped and
	 * report "unavailable" for a boot whose cause is sitting in the ring.
	 */
	whoami_history_begin();

	/*
	 * Deliberately not a busy loop and deliberately not a k_sleep(FOREVER)
	 * either: main returning is fine in Zephyr, the shell and the network
	 * stack run on their own threads, and leaving a thread parked here
	 * would only cost a stack.
	 */
	return 0;
}
