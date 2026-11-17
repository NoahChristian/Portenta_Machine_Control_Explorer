/*
 * Shell commands for seeing, and undoing, what MCUboot decided.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 *
 * WHY THESE EXIST AT ALL. The shipping bootloader is built with no console
 * (mcuboot/pmc.conf turns logging off and build.ps1 passes CONFIG_CONSOLE=n),
 * so when it declines an upgrade it does so in SILENCE. These commands print
 * the same inputs its decision is made from, from the one side that does have
 * a working console.
 *
 * On the Giga - same SoC, same asymmetric layout - three separate silent
 * failures each took a session to find, and in every case the missing
 * information was exactly what `mcuboot` prints here. They are written BEFORE
 * the first flash on purpose: a diagnostic added after the thing it diagnoses
 * has already gone wrong is a diagnostic written while guessing.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/shell/shell.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/dfu/mcuboot.h>

/* The magic MCUboot stamps at the head of a signed image. */
#define PMC_IMG_MAGIC 0x96f3b83dUL

static const char *swap_type_str(int t)
{
	switch (t) {
	case BOOT_SWAP_TYPE_NONE:   return "NONE (boot what is in slot0)";
	case BOOT_SWAP_TYPE_TEST:   return "TEST (swap once; reverts unless confirmed)";
	case BOOT_SWAP_TYPE_PERM:   return "PERM (swap and keep)";
	case BOOT_SWAP_TYPE_REVERT: return "REVERT (undo the last swap)";
	case BOOT_SWAP_TYPE_FAIL:   return "FAIL (swap was attempted and failed)";
	default:                    return "unrecognized";
	}
}

/*
 * THE SAME FACTS, FOR THE PANEL. With the serial port gone this is the only
 * way to see a revert armed or a swap that failed - and a failed swap is the
 * one condition that silently disables every future update.
 */
const char *pmc_swap_type_name(void)
{
	return swap_type_str(mcuboot_swap_type());
}

bool pmc_img_confirmed(void)
{
	return boot_is_img_confirmed();
}

/*
 * Geometry, which is what boot_slots_compatible() actually walks.
 *
 * Printed for every area because the whole reason this layout needs
 * swap-using-scratch is that the numbers differ: 128 KB sectors in internal
 * flash against 4 KB in the QSPI NOR. swap-using-move requires them to match
 * and fails silently when they do not, so seeing them side by side is the
 * fastest way to know which algorithm can possibly work.
 */
static void print_area(const struct shell *sh, const char *name, uint8_t id)
{
	const struct flash_area *fa;
	int rc = flash_area_open(id, &fa);

	if (rc != 0) {
		shell_print(sh, "  %-18s open failed (%d)", name, rc);
		return;
	}

	const struct device *dev = flash_area_get_device(fa);
	struct flash_pages_info pg;
	size_t sector = 0;
	size_t count = 0;

	if (dev != NULL && flash_get_page_info_by_offs(dev, fa->fa_off, &pg) == 0) {
		sector = pg.size;
		if (sector != 0) {
			count = fa->fa_size / sector;
		}
	}

	shell_print(sh, "  %-18s off 0x%08lx  size 0x%06zx  sector %-7zu x%-5zu  %s",
		    name, (unsigned long)fa->fa_off, (size_t)fa->fa_size,
		    sector, count, dev != NULL ? dev->name : "(no device)");

	flash_area_close(fa);
}

/*
 * The image header, which is what boot_check_header_erased() tests.
 *
 * A slot whose magic is not PMC_IMG_MAGIC holds no image MCUboot will
 * consider - and "magic=unset" on the PRIMARY slot is normal and benign here,
 * because an image written straight to slot0 over DFU or SWD carries no
 * trailer at all. With the magic erased there is no pending swap for MCUboot
 * to act on, which is exactly what should be true of a freshly installed
 * image.
 */
static void print_header(const struct shell *sh, const char *name, uint8_t id)
{
	struct mcuboot_img_header hdr;
	int rc = boot_read_bank_header(id, &hdr, sizeof(hdr));

	if (rc != 0) {
		shell_print(sh, "  %-18s no readable image header (%d)", name, rc);
		return;
	}

	/*
	 * struct mcuboot_img_header carries the MCUboot header VERSION and a
	 * union of per-version payloads; it deliberately omits fields "not
	 * currently relevant to applications", which includes header_size. So
	 * the 0x400 header size is not readable here - imgtool dumpinfo on the
	 * host is where that gets checked.
	 */
	shell_print(sh, "  %-18s v%u.%u.%u+%u  image size %u  (hdr format v%u)",
		    name,
		    hdr.h.v1.sem_ver.major, hdr.h.v1.sem_ver.minor,
		    hdr.h.v1.sem_ver.revision, hdr.h.v1.sem_ver.build_num,
		    hdr.h.v1.image_size, hdr.mcuboot_version);
}

static int cmd_mcuboot(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "slot geometry (what boot_slots_compatible walks):");
#if PARTITION_EXISTS(boot_partition)
	print_area(sh, "boot", PARTITION_ID(boot_partition));
#endif
#if PARTITION_EXISTS(slot0_partition)
	print_area(sh, "slot0 (primary)", PARTITION_ID(slot0_partition));
#endif
#if PARTITION_EXISTS(slot1_partition)
	print_area(sh, "slot1 (secondary)", PARTITION_ID(slot1_partition));
#endif
#if PARTITION_EXISTS(scratch_partition)
	print_area(sh, "scratch", PARTITION_ID(scratch_partition));
#endif

	/*
	 * The scratch rule, checked here rather than left to be rediscovered.
	 * boot_slots_compatible() only rejects a sector STRICTLY larger than
	 * scratch, so an exactly-equal scratch passes the compatibility check
	 * and then fails during the swap itself, because MCUboot writes a
	 * scratch trailer into the same area.
	 */
#if PARTITION_EXISTS(scratch_partition) && PARTITION_EXISTS(slot0_partition)
	{
		const struct flash_area *fs, *f0;

		if (flash_area_open(PARTITION_ID(scratch_partition), &fs) == 0 &&
		    flash_area_open(PARTITION_ID(slot0_partition), &f0) == 0) {
			const struct device *d0 = flash_area_get_device(f0);
			struct flash_pages_info pg0;

			if (d0 != NULL &&
			    flash_get_page_info_by_offs(d0, f0->fa_off, &pg0) == 0) {
				shell_print(sh, "");
				if (fs->fa_size > pg0.size) {
					shell_print(sh, "  scratch 0x%zx > largest sector 0x%zx: ok",
						    (size_t)fs->fa_size, pg0.size);
				} else {
					shell_error(sh, "  scratch 0x%zx <= largest sector 0x%zx:",
						    (size_t)fs->fa_size, pg0.size);
					shell_error(sh, "  the compatibility check will PASS and the "
							"swap will then fail");
				}
			}
			flash_area_close(fs);
			flash_area_close(f0);
		}
	}
#endif

	shell_print(sh, "");
	shell_print(sh, "image headers (magic must be 0x%08lx):", PMC_IMG_MAGIC);
#if PARTITION_EXISTS(slot0_partition)
	print_header(sh, "slot0", PARTITION_ID(slot0_partition));
#endif
#if PARTITION_EXISTS(slot1_partition)
	print_header(sh, "slot1", PARTITION_ID(slot1_partition));
#endif

	shell_print(sh, "");
	shell_print(sh, "running image:");
	shell_print(sh, "  confirmed   %s", boot_is_img_confirmed() ? "yes" : "NO - will revert on reset");
	shell_print(sh, "  swap type   %s", swap_type_str(mcuboot_swap_type()));
	shell_print(sh, "  built from  %s", CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION);

	return 0;
}

/*
 * ONE FAILED SWAP QUIETLY DISABLES OTA FOREVER, AND THIS IS THE ONLY WAY BACK.
 *
 * MCUboot writes swap_info into the PRIMARY slot's trailer before it starts
 * moving sectors. If the swap dies partway, that byte stays. On the next boot
 * swap_read_status() reads it, gets a non-NONE swap type, and takes the
 * "resume a swap I already decided on" branch in
 * boot_prepare_image_for_update() instead of consulting boot_swap_tables - so
 * a freshly staged update is IGNORED and left pending. The only outward sign is
 * a missing "Image index: N, Swap type: ..." line, which nobody sees because
 * the shipping bootloader has no console.
 *
 * Measured on the Giga: clearing scratch and slot1 is NOT enough. The slot0
 * trailer is the one that is easy to leave out and then spend an afternoon on.
 *
 * The erase length is read from each area's OWN geometry rather than
 * hardcoded, because a fixed 64 KB is legal on internal flash and rejected on
 * the QSPI NOR - which would leave the slot0 trailer uncleared, i.e. exactly
 * the failure this command exists to undo.
 */
static int erase_last_sector(const struct shell *sh, const char *name, uint8_t id)
{
	const struct flash_area *fa;
	int rc = flash_area_open(id, &fa);

	if (rc != 0) {
		shell_error(sh, "  %s: open failed (%d)", name, rc);
		return rc;
	}

	const struct device *dev = flash_area_get_device(fa);
	struct flash_pages_info pg;
	int ret = 0;

	if (dev == NULL) {
		shell_error(sh, "  %s: no device", name);
		ret = -ENODEV;
		goto out;
	}

	/* Geometry at the END of the area, which is where the trailer lives. */
	if (flash_get_page_info_by_offs(dev, fa->fa_off + fa->fa_size - 1, &pg) != 0) {
		shell_error(sh, "  %s: cannot read sector geometry", name);
		ret = -EIO;
		goto out;
	}

	const off_t off = fa->fa_size - pg.size;

	rc = flash_area_erase(fa, off, pg.size);
	if (rc != 0) {
		shell_error(sh, "  %s: erase of 0x%zx at +0x%lx failed (%d)",
			    name, pg.size, (unsigned long)off, rc);
		ret = rc;
		goto out;
	}

	shell_print(sh, "  %s: erased 0x%zx at +0x%lx", name, pg.size, (unsigned long)off);

out:
	flash_area_close(fa);
	return ret;
}

static int cmd_swapclear(const struct shell *sh, size_t argc, char **argv)
{
	/*
	 * Guarded on the DESTRUCTIVE direction only, and with no default: this
	 * erases trailers, and getting it wrong on a board mid-update loses the
	 * staged image. Naming the target explicitly means a mistyped command
	 * does nothing rather than something.
	 */
	if (argc != 2 || strcmp(argv[1], "confirm") != 0) {
		shell_print(sh, "usage: swapclear confirm");
		shell_print(sh, "");
		shell_print(sh, "Erases the swap state so OTA works again after a FAILED swap:");
		shell_print(sh, "  the whole scratch area, slot1's last sector, and");
		shell_print(sh, "  slot0's last sector - which is the one that matters and");
		shell_print(sh, "  the one that is easy to leave out.");
		shell_print(sh, "");
		shell_print(sh, "Does not touch either image. Run `mcuboot` first.");
		return -EINVAL;
	}

	int fail = 0;

	shell_print(sh, "clearing swap state:");

#if PARTITION_EXISTS(scratch_partition)
	{
		const struct flash_area *fa;

		if (flash_area_open(PARTITION_ID(scratch_partition), &fa) == 0) {
			int rc = flash_area_erase(fa, 0, fa->fa_size);

			if (rc == 0) {
				shell_print(sh, "  scratch: erased 0x%zx (all)",
					    (size_t)fa->fa_size);
			} else {
				shell_error(sh, "  scratch: erase failed (%d)", rc);
				fail = 1;
			}
			flash_area_close(fa);
		}
	}
#endif
#if PARTITION_EXISTS(slot1_partition)
	if (erase_last_sector(sh, "slot1  ", PARTITION_ID(slot1_partition)) != 0) {
		fail = 1;
	}
#endif
#if PARTITION_EXISTS(slot0_partition)
	if (erase_last_sector(sh, "slot0  ", PARTITION_ID(slot0_partition)) != 0) {
		fail = 1;
	}
#endif

	shell_print(sh, "");
	if (fail) {
		shell_error(sh, "one or more erases failed - OTA is still blocked");
		shell_error(sh, "a failure on slot0 above 1 MB is the bank-2 PGSERR trap:");
		shell_error(sh, "check that flash0's size has not been overridden");
		return -EIO;
	}

	shell_print(sh, "swap state cleared. Stage an update and reset.");
	return 0;
}

SHELL_CMD_REGISTER(mcuboot, NULL,
		   "What MCUboot decides from: slot geometry, image headers, swap type.",
		   cmd_mcuboot);

SHELL_CMD_ARG_REGISTER(swapclear, NULL,
		       "Clear swap state after a failed swap (swapclear confirm).",
		       cmd_swapclear, 1, 1);
