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
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>          /* STRINGIFY, for BUILD_VERSION */
#include <zephyr/version.h>           /* KERNEL_VERSION_STRING, BUILD_VERSION */

#include <stdio.h>
#include <string.h>
#include <time.h>

LOG_MODULE_REGISTER(whoami, LOG_LEVEL_INF);

/*
 * THE APPID LINE. One format, shared by every board on this bench:
 *
 *     appid <token> [unit <n>] fw <major.minor.patch>
 *
 *   token   lowercase and hyphenated, unique to the application, and NEVER
 *           renamed or reused -- scripts match on it to decide which board
 *           they are holding, so it is a contract rather than a label.
 *   unit    present only where a project has several physical units. There is
 *           one of this board, so the field is absent rather than always "1".
 *   fw      LAST, so a parser can take the final whitespace-separated field
 *           without knowing whether `unit` was there.
 *
 * WHY THIS BOARD NEEDS IT MOST. Every Zephyr CDC device on this bench
 * enumerates as 2FE3:0004, so VID and PID identify the RTOS and nothing else.
 * On 2026-11-16 THIS board's `health` output -- expanders, analog switches, a
 * panel, none of which exist on the other one -- was read as the other
 * board's for several minutes. That is the incident the whole whoami family
 * came from, and until now this was the only one of the three that could not
 * state its own name in one greppable line.
 */
#define WHOAMI_APP "portenta-machine-control"

int whoami_app_id(char *buf, size_t cap)
{
	if (!buf || cap == 0) {
		return -EINVAL;
	}
	const int n = snprintf(buf, cap, "appid " WHOAMI_APP " fw %s",
			       CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION);

	if (n < 0 || (size_t)n >= cap) {
		return -ENOMEM;
	}
	return n;
}

/*
 * BOOT HISTORY. Ported from the sibling boards, where its absence is what
 * made a reset unexplainable: the cause register describes only the boot you
 * are standing in, so restarting a board to get it working destroys the
 * record of why it stopped. One settings write per boot keeps it.
 */
#define WHOAMI_TREE    "whoami"
#define WHOAMI_RING    WHOAMI_TREE "/ring"
#define WHOAMI_HISTORY 8

struct boot_rec {
	uint32_t cause;   /* the hwinfo flags word, undecoded */
	uint32_t epoch;   /* 0 when that boot had no clock yet */
} __packed;

static struct boot_rec s_ring[WHOAMI_HISTORY];
static uint8_t         s_ring_count;
static bool            s_ring_loaded;
static uint32_t        s_reset_cause;
static bool            s_reset_cause_known;

static int whoami_settings_set(const char *name, size_t len,
			       settings_read_cb read_cb, void *cb_arg)
{
	const char *next;

	if (settings_name_steq(name, "ring", &next) && !next) {
		if (len != sizeof(s_ring)) {
			/* Written by a different build. Start over rather
			 * than decode a layout that has changed. */
			return 0;
		}
		if (read_cb(cb_arg, s_ring, sizeof(s_ring)) > 0) {
			s_ring_loaded = true;
		}
		return 0;
	}
	return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(pmc_whoami, WHOAMI_TREE, NULL,
			       whoami_settings_set, NULL, NULL);

/*
 * ONE decoder, taking the value as an argument so this boot and every stored
 * boot are named by the same table. The buffer is the caller's, so two
 * decodes can be alive at once -- a shared static would let the second call
 * rewrite the first one's answer inside a single printf argument list.
 */
static const char *cause_to(uint32_t cause, char *s, size_t cap)
{
	s[0] = '\0';
	if (cause & RESET_POR)            strncat(s, "POR ", cap - strlen(s) - 1);
	if (cause & RESET_PIN)            strncat(s, "PIN ", cap - strlen(s) - 1);
	if (cause & RESET_WATCHDOG)       strncat(s, "WATCHDOG ", cap - strlen(s) - 1);
	if (cause & RESET_SOFTWARE)       strncat(s, "SOFTWARE ", cap - strlen(s) - 1);
	if (cause & RESET_BROWNOUT)       strncat(s, "BROWNOUT ", cap - strlen(s) - 1);
	if (cause & RESET_CPU_LOCKUP)     strncat(s, "CPU_LOCKUP ", cap - strlen(s) - 1);
	if (cause & RESET_LOW_POWER_WAKE) strncat(s, "LOW_POWER_WAKE ", cap - strlen(s) - 1);

	if (s[0] == '\0') {
		snprintf(s, cap, "0x%08x (no known bit set)", cause);
	} else {
		s[strlen(s) - 1] = '\0';   /* the trailing space */
	}
	return s;
}

void whoami_history_begin(void)
{
	uint32_t cause = 0;

	if (hwinfo_get_reset_cause(&cause) == 0) {
		s_reset_cause = cause;
		s_reset_cause_known = true;
		/*
		 * CLEARED, so the next boot reads its own cause and not a
		 * union of every boot since power-on. That is what makes the
		 * value worth storing -- and also what makes storing it
		 * necessary, since clearing is what puts it out of reach of
		 * anyone asking later.
		 */
		(void)hwinfo_clear_reset_cause();
	}

	if (settings_load_subtree(WHOAMI_TREE) != 0) {
		LOG_WRN("boot history unavailable; this boot will not be recorded");
	}

	s_ring_count = 0;
	if (s_ring_loaded) {
		for (uint8_t i = 0; i < WHOAMI_HISTORY; i++) {
			if (s_ring[i].cause == 0u && s_ring[i].epoch == 0u) {
				break;
			}
			s_ring_count++;
		}
	}

	for (uint8_t i = WHOAMI_HISTORY - 1; i > 0; i--) {
		s_ring[i] = s_ring[i - 1];
	}
	s_ring[0].cause = s_reset_cause_known ? s_reset_cause : 0u;
	s_ring[0].epoch = (uint32_t)time(NULL);
	if (s_ring[0].epoch < 1767225600u) {   /* 2026-01-01 */
		s_ring[0].epoch = 0u;            /* happened; time unknown */
	}
	if (s_ring_count < WHOAMI_HISTORY) {
		s_ring_count++;
	}

	(void)settings_save_one(WHOAMI_RING, s_ring, sizeof(s_ring));
}

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
		shell_print(sh, "image      %u.%u.%u+%u  slot0, %s",
			    hdr.h.v1.sem_ver.major,
			    hdr.h.v1.sem_ver.minor,
			    hdr.h.v1.sem_ver.revision,
			    hdr.h.v1.sem_ver.build_num,
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

	/*
	 * The matchable line FIRST and verbatim, as on the sibling boards, so
	 * a script may parse any of the three the same way and the human and
	 * machine forms cannot drift -- they are the same bytes.
	 */
	char id[72];

	if (whoami_app_id(id, sizeof(id)) > 0) {
		shell_print(sh, "%s", id);
	}
	shell_print(sh, "project    Portenta Machine Control Explorer");
	shell_print(sh, "hostname   %s  (the name to match on)",
		    net_hostname_get());
	shell_print(sh, "board      %s", CONFIG_BOARD_TARGET);
	print_image(sh);
	/*
	 * The build stamp separates two COMPILES carrying the same version,
	 * which the +build counter cannot: that counter moves when
	 * tools/bumpver.py is run, and a bench session usually compiles
	 * several times between bumps. So the line above answers "which image
	 * is this" and this one answers "is it the one I just built".
	 */
	shell_print(sh, "built      %s %s   (this firmware)", __DATE__, __TIME__);
	/*
	 * `core` beside `built`, matching the sibling boards. Here the whole
	 * image is compiled from source, so __DATE__ covers it -- but the
	 * kernel version is still the thing an upstream bug report has to
	 * quote, and a field that means the application on one board and the
	 * toolchain on another is a trap however correct each half is.
	 */
	shell_print(sh, "core       Zephyr %s (%s)   (compiled from source here)",
		    KERNEL_VERSION_STRING, STRINGIFY(BUILD_VERSION));
	shell_print(sh, "uptime     %ud %02u:%02u:%02u",
		    (unsigned int)(up / 86400U),
		    (unsigned int)((up / 3600U) % 24U),
		    (unsigned int)((up / 60U) % 60U),
		    (unsigned int)(up % 60U));

	char now[64];

	shell_print(sh, "reset      %s",
		    s_reset_cause_known ? cause_to(s_reset_cause, now, sizeof(now))
					: "unavailable");

	if (s_ring_count < 2) {
		shell_print(sh, "previous   no record yet - history starts at "
				"this boot");
	} else {
		char prev[64];

		(void)cause_to(s_ring[1].cause, prev, sizeof(prev));
		if (s_ring[1].epoch == 0u) {
			shell_print(sh, "previous   %s   (no clock at that boot)",
				    prev);
		} else {
			char when[32];
			time_t t = (time_t)s_ring[1].epoch;
			struct tm tmv;

			gmtime_r(&t, &tmv);
			strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &tmv);
			shell_print(sh, "previous   %s   started %s", prev, when);
		}
	}
	return 0;
}

/* `whoami id` -- the one line, for a script that wants to match and move on.
 * Not a different answer, literally the same function. */
static int cmd_whoami_id(const struct shell *sh, size_t argc, char **argv)
{
	char id[72];

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (whoami_app_id(id, sizeof(id)) <= 0) {
		shell_error(sh, "identity did not fit its buffer");
		return -ENOMEM;
	}
	shell_print(sh, "%s", id);
	return 0;
}

/* The same bytes as `whoami`'s first line, kept as its own command because a
 * detection script should not have to read a whole report to match one
 * token. */
SHELL_CMD_REGISTER(appid, NULL, "Identify this application to a script",
		   cmd_whoami_id);

SHELL_CMD_REGISTER(whoami, NULL,
		   "Which board, which image, since when.", cmd_whoami);
