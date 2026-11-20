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
 * ONE DELIBERATE OMISSION. No IP and no MAC. This output is meant to be pasted
 * verbatim into docs/MEASURED.md, which is the whole value of captured output,
 * and an address in a committed file is not allowed here. `net iface` prints
 * them when they are what you actually want.
 *
 * THE REPORT IS A DOCUMENTED FORMAT, not this board's own layout: every line
 * is a label in an 11-column field followed by its value, the first two lines
 * are a frozen preamble, and a caller names the schema major it speaks. The
 * grammar, the required fields and the reasoning are in the whoami spec; this
 * file is one emitter of it.
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
#include <stdlib.h>
#include <string.h>
#include <time.h>

LOG_MODULE_REGISTER(whoami, LOG_LEVEL_INF);

/*
 * THE SCHEMA THIS EMITS, and the majors it can speak.
 *
 * The major is the FORMAT: a reader that does not know it cannot parse the
 * body at all, because a familiar label may mean something else under another
 * major. The revision is corrections only and never moves a field, so any
 * reader of major 1 reads every 1.xxxx - it is four digits and zero-padded
 * because `1.12` cannot be told from `1.1200`, and the two orderings disagree.
 *
 * This board emits major 1 and speaks nothing else. Emitting an older major
 * would cost flash on the constrained side of the link and no device is
 * obliged to; SAYING so is free, and it is what lets a caller report "speaks
 * 1, I wanted 2" instead of a parse failure.
 */
#define WHOAMI_SCHEMA "1.0005"
#define WHOAMI_MAJOR  1UL
#define WHOAMI_SPEAKS "1"

/* The label field is 11 columns wide and the value starts at column 12. A
 * label is parsed by SLICING that field, not by searching for whitespace,
 * which is what lets a label contain a space with no escaping. Every line
 * below pads to this width by hand, so the width is written here once to be
 * checked against. */
#define WHOAMI_LABEL_WIDTH 11

/*
 * THE APPID LINE. One format, shared by every board on this bench:
 *
 *     appid      <token> [unit <n>] fw <major.minor.patch>
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
 *
 * IT IS A REPORT LINE AND A COMMAND, AND THEY ARE THE SAME BYTES. The label is
 * padded into the 11-column field here rather than at one of the two call
 * sites, so `appid` on its own and the `appid` line inside `whoami` cannot
 * come out differently -- there is one string and one definition of it.
 */
#define WHOAMI_APP "portenta-machine-control"

/* The one padded label in this file that is not written beside its own line,
 * so it is the one a reader cannot check by eye against its neighbors. */
BUILD_ASSERT(sizeof("appid      ") - 1 == WHOAMI_LABEL_WIDTH,
	     "the appid label must fill the 11-column field exactly");

int whoami_app_id(char *buf, size_t cap)
{
	if (!buf || cap == 0) {
		return -EINVAL;
	}
	const int n = snprintf(buf, cap, "appid      " WHOAMI_APP " fw %s",
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

/*
 * THE FROZEN PREAMBLE. These two lines have the same grammar under every
 * schema major that will ever exist, which is what makes them readable by a
 * caller that cannot parse the body - and that is the whole mechanism: a
 * reader meeting a major it does not know can still discover WHICH one it is
 * looking at and WHAT ELSE this device could have said.
 *
 * ONE function, called by the report and by the refusal, so the two can never
 * disagree about what this board speaks.
 */
static void print_preamble(const struct shell *sh)
{
	shell_print(sh, "schema     " WHOAMI_SCHEMA);
	shell_print(sh, "speaks     " WHOAMI_SPEAKS);
}

static int cmd_whoami(const struct shell *sh, size_t argc, char **argv)
{
	const uint64_t up = (uint64_t)k_uptime_get() / 1000U;
	unsigned long want = WHOAMI_MAJOR;

	/*
	 * THE CALLER DECLARES FIRST. `whoami <major>` says what the asker can
	 * read, and this board answers in that major or not at all. A bare
	 * `whoami` is the human form and gets this board's own newest, which
	 * is why a TOOL may not use it.
	 */
	if (argc > 1) {
		char *end;

		want = strtoul(argv[1], &end, 10);
		if (end == argv[1] || *end != '\0') {
			shell_error(sh, "usage: whoami [<schema major>]");
			return -EINVAL;
		}
	}

	print_preamble(sh);

	/*
	 * ASKED FOR A MAJOR THIS BOARD DOES NOT SPEAK: the preamble and
	 * nothing else. Not this board's own newest report, which the caller
	 * has already said it cannot read, and not silence - the caller can
	 * always read those two lines, so it ends with "speaks 1, I wanted N"
	 * rather than with a parse failure or a timeout.
	 *
	 * This is the path that only runs on the day a new major ships, so it
	 * is the path most likely to be wrong and never noticed. Ask for a
	 * version nobody implements (`whoami 9999`) and watch it stop here.
	 */
	if (want != WHOAMI_MAJOR) {
		return 0;
	}

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
	shell_print(sh, "board      %s", CONFIG_BOARD_TARGET);
	/*
	 * The build stamp separates two COMPILES carrying the same version,
	 * which the +build counter cannot: that counter moves when
	 * tools/bumpver.py is run, and a bench session usually compiles
	 * several times between bumps. So the image line below answers "which
	 * image is this" and this one answers "is it the one I just built".
	 */
	shell_print(sh, "built      %s %s   (this firmware)", __DATE__, __TIME__);
	shell_print(sh, "uptime     %ud %02u:%02u:%02u",
		    (unsigned int)(up / 86400U),
		    (unsigned int)((up / 3600U) % 24U),
		    (unsigned int)((up / 60U) % 60U),
		    (unsigned int)(up % 60U));
	/*
	 * WHAT THIS BOARD COULD PROVE, not a claim about this report. The
	 * report is unauthenticated by definition - anything that can answer
	 * on the port can produce one - so the field says what is on offer,
	 * and `none` is the honest answer here: there is no per-device key and
	 * no secure element behind it, so there is nothing to sign with.
	 */
	shell_print(sh, "attest     none");

	/*
	 * Everything above is required by the schema and everything below is
	 * recommended-or-extra, in that order, so a reader has the fields it
	 * must have before it meets anything it is allowed not to understand.
	 */
	shell_print(sh, "hostname   %s  (the name to match on)",
		    net_hostname_get());
	print_image(sh);
	/*
	 * `core` beside `built`, matching the sibling boards. Here the whole
	 * image is compiled from source, so __DATE__ covers it -- but the
	 * kernel version is still the thing an upstream bug report has to
	 * quote, and a field that means the application on one board and the
	 * toolchain on another is a trap however correct each half is.
	 */
	shell_print(sh, "core       Zephyr %s (%s)   (compiled from source here)",
		    KERNEL_VERSION_STRING, STRINGIFY(BUILD_VERSION));

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

/* `appid` -- the one line, for a script that wants to match and move on.
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

/* The same bytes as the `appid` line inside the report, kept as its own
 * command because a detection script should not have to read a whole report
 * to match one token. */
SHELL_CMD_REGISTER(appid, NULL, "Identify this application to a script",
		   cmd_whoami_id);

/*
 * One optional argument, and it is an ARGUMENT rather than a subcommand: a
 * bare number is the major the caller speaks, and the shell would have to
 * carry a child entry per version to treat it as a name.
 */
SHELL_CMD_ARG_REGISTER(whoami, NULL,
		       "whoami [<schema major>] - which board, which image, "
		       "since when.", cmd_whoami, 1, 1);
