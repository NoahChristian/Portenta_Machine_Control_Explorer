/*
 * `whoami` - which board, which image, since when.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 *
 * WHY THIS EXISTS. A thread-stack capture on 2026-11-18 walked the COM ports,
 * took the first one that answered `kernel thread list`, and returned 19
 * plausible threads from a DIFFERENT Zephyr board on the same bench - same
 * CDC-ACM shell, same subsystem thread names, different firmware. The tell was
 * an absence (no application thread in the list), which first read as a
 * truncated capture rather than as the wrong board. A measurement from an
 * unidentified port is a measurement of an unidentified board.
 *
 * `mdnsname` already answered "which project" and is what tools/ota.ps1 asks
 * before it does anything. This answers the whole question in one round trip:
 * project, board target, image version, which slot it is running from, whether
 * it is confirmed, and when it was built. A script that asks one question and
 * gets one answer has nowhere left to be wrong.
 *
 * TWO DELIBERATE OMISSIONS.
 *
 * No second identity token. The matchable string is the hostname, the same one
 * mdnsname prints and the same one the tools already grep for - a `whoami`
 * that minted its own project string would be a second thing to keep in step,
 * and the first time they drifted the scripts would be matching the one that
 * was not authoritative.
 *
 * No IP and no MAC. This output is meant to be pasted verbatim into
 * docs/MEASURED.md, which is the whole value of captured output, and an
 * address in a committed file is not allowed here. `net iface` prints them
 * when they are what you actually want.
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/net/hostname.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/dfu/mcuboot.h>

/*
 * The running image's version read from the header MCUboot stamped, not from
 * the Kconfig string the build was configured with. With swap-using-scratch
 * the running image is always the one in slot0, so that is the header to read:
 * after a swap, slot1 holds the image this one replaced.
 *
 * Both are printed, because they answer different questions. The header is
 * what is in flash; CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION is what this source
 * tree was configured to sign. They disagree when an image was flashed from
 * somewhere other than the tree in front of you - which is exactly the state
 * that is impossible to see from the outside.
 */
static void print_image(const struct shell *sh)
{
	struct mcuboot_img_header hdr;

#if PARTITION_EXISTS(slot0_partition)
	if (boot_read_bank_header(PARTITION_ID(slot0_partition), &hdr,
				  sizeof(hdr)) == 0) {
		shell_print(sh, "image      %u.%u.%u  slot0, %s",
			    hdr.h.v1.sem_ver.major,
			    hdr.h.v1.sem_ver.minor,
			    hdr.h.v1.sem_ver.revision,
			    boot_is_img_confirmed()
				    ? "confirmed"
				    : "NOT confirmed - reverts on reset");
	} else {
		/*
		 * No readable header in slot0 while this image is plainly
		 * running means it was written past MCUboot - straight to
		 * 0x08040000 over DFU or SWD - so the bootloader never saw it
		 * and the OTA path was never exercised.
		 */
		shell_print(sh, "image      no MCUboot header in slot0: this "
				"image was not installed by the bootloader");
	}
#else
	shell_print(sh, "image      built with no slot0 partition");
#endif
	shell_print(sh, "signed     %s  (what this source tree configures)",
		    CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION);
}

static int cmd_whoami(const struct shell *sh, size_t argc, char **argv)
{
	const uint64_t up = (uint64_t)k_uptime_get() / 1000U;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "project    Portenta Machine Control Explorer");
	shell_print(sh, "hostname   %s  (the name to match on)",
		    net_hostname_get());
	shell_print(sh, "board      %s", CONFIG_BOARD_TARGET);
	print_image(sh);
	/*
	 * The build stamp is the only thing that separates two builds carrying
	 * the same version number, which is the usual state of a bench session:
	 * the version changes when an OTA is cut, not when a fix is compiled.
	 */
	shell_print(sh, "built      %s %s", __DATE__, __TIME__);
	shell_print(sh, "uptime     %ud %02u:%02u:%02u",
		    (unsigned int)(up / 86400U),
		    (unsigned int)((up / 3600U) % 24U),
		    (unsigned int)((up / 60U) % 60U),
		    (unsigned int)(up % 60U));
	return 0;
}

SHELL_CMD_REGISTER(whoami, NULL,
		   "Which board, which image, since when.", cmd_whoami);
