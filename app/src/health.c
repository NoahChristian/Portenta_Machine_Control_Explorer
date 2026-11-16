/*
 * The health gate: the conditions under which this image confirms itself.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 *
 * WHAT THIS IS FOR. MCUboot boots a freshly uploaded image in TEST mode. If
 * nothing calls boot_write_img_confirmed() before the next reset, the previous
 * image is swapped back. So the question "is this image good?" has to be
 * answered by the image itself, and the answer is only as good as the question.
 *
 * "main() was reached" proves almost nothing - it proves the vector table is
 * valid and the kernel started. The Giga port's gate was a live MQTT broker
 * connection, on the reasoning that getting that far proves the display, WiFi,
 * TLS and credentials all work. There is no broker here, so the equivalent for
 * a board whose entire job is I/O is: the I/O answers, and the thing that would
 * let you fix a bad image is running.
 *
 * THAT LAST CONDITION IS THE IMPORTANT ONE. An image that cannot be updated is
 * the worst outcome of all - worse than one that crashes, because a crash
 * reverts and a confirmed-but-unreachable image does not. So the SMP transport
 * being up is a gate condition, not an afterthought.
 *
 * EVERY CONDITION REPORTS WHY IT FAILED. An image that will not confirm must
 * say so, or the symptom is a board that silently reverts every update with no
 * indication which subsystem is unhappy.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>

#include <stm32_ll_system.h>

#include "status_out.h"
#include "health.h"

LOG_MODULE_REGISTER(pmc_health, LOG_LEVEL_INF);

/*
 * Minimum uptime before confirming.
 *
 * Deliberately not zero. A crash loop that satisfies every other condition
 * within its first second would otherwise confirm itself and then keep
 * crashing, with the good image already swapped away. Sixty seconds is long
 * enough to have tried to do some work and short enough not to be annoying on
 * the bench.
 */
#define PMC_HEALTH_MIN_UPTIME_S 60

/* The three SYSCFG analog switches this carrier needs open. */
#define PMC_AI_SWITCHES (LL_SYSCFG_ANALOG_SWITCH_PA1 | \
			 LL_SYSCFG_ANALOG_SWITCH_PC2 | \
			 LL_SYSCFG_ANALOG_SWITCH_PC3)

struct health_check {
	const char *name;
	const char *why;          /* what it proves, for the report */
	bool (*test)(const char **detail);
};

/* --- the checks ----------------------------------------------------------- */

static bool chk_uptime(const char **detail)
{
	const int64_t up = k_uptime_get() / 1000;

	if (up < PMC_HEALTH_MIN_UPTIME_S) {
		static char buf[48];

		snprintk(buf, sizeof(buf), "%lld s of %d",
			 (long long)up, PMC_HEALTH_MIN_UPTIME_S);
		*detail = buf;
		return false;
	}
	return true;
}

struct link_scan {
	bool any_up;
	bool any_addr;
};

static void link_check_one(struct net_if *iface, void *user_data)
{
	struct link_scan *scan = user_data;

	if (!net_if_is_up(iface)) {
		return;
	}
	scan->any_up = true;

#if defined(CONFIG_NET_IPV4)
	if (net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED) != NULL) {
		scan->any_addr = true;
	}
#endif
}

/*
 * ANY interface with an address, not the DEFAULT one.
 *
 * net_if_get_default() returns interface 1, which on this board is eth0 - the
 * Ethernet port, which has no cable in it and never comes up. So the first
 * version of this check reported "interface down" while wlan0 sat there
 * associated, DHCP-bound and answering SMP requests. The board was reachable;
 * the check was looking somewhere else and saying so with complete confidence.
 *
 * That is the same mistake as every other one worth recording today: not a
 * wrong answer to the right question, but a confident answer to the wrong one.
 * What this gate actually needs to know is "can this image be reached", and no
 * part of that question mentions which interface.
 */
static bool chk_link(const char **detail)
{
	struct link_scan scan = { false, false };

	net_if_foreach(link_check_one, &scan);

	if (!scan.any_up) {
		*detail = "no interface is up";
		return false;
	}
	if (!scan.any_addr) {
		*detail = "an interface is up but none has an IPv4 address";
		return false;
	}
	return true;
}

/*
 * The SMP transport, checked indirectly.
 *
 * There is no public "is the SMP UDP server bound" API, and adding one would
 * mean patching mcumgr. CONFIG_MCUMGR_TRANSPORT_UDP_AUTOMATIC_INIT=y means the
 * transport comes up with the network, so a usable IPv4 address is the
 * strongest signal available from here - which is why this is deliberately NOT
 * counted as an independent check, only noted. If the day comes that an image
 * confirms itself and then proves unreachable, THIS is the assumption to go
 * after first.
 */
static bool chk_io_expanders(const char **detail)
{
#if DT_NODE_EXISTS(DT_NODELABEL(pmc_din_expander)) && \
	DT_NODE_EXISTS(DT_NODELABEL(pmc_pdio_expander))
	const struct device *din = DEVICE_DT_GET(DT_NODELABEL(pmc_din_expander));
	const struct device *pdio = DEVICE_DT_GET(DT_NODELABEL(pmc_pdio_expander));

	if (!device_is_ready(din)) {
		*detail = "digital-input expander did not answer on I2C";
		return false;
	}
	if (!device_is_ready(pdio)) {
		*detail = "programmable-DIO expander did not answer on I2C";
		return false;
	}
	return true;
#else
	*detail = "no expander nodes in this build";
	return false;
#endif
}

static bool chk_analog_switches(const char **detail)
{
	const uint32_t pmcr = SYSCFG->PMCR & PMC_AI_SWITCHES;

	if (pmcr != PMC_AI_SWITCHES) {
		static char buf[64];

		snprintk(buf, sizeof(buf), "PMCR 0x%08x, wanted 0x%08x",
			 pmcr, (uint32_t)PMC_AI_SWITCHES);
		*detail = buf;
		return false;
	}
	return true;
}

static atomic_t s_http_200;

void pmc_health_note_http_200(void)
{
	atomic_set(&s_http_200, 1);
}

/*
 * WAIVED AFTER A GRACE PERIOD, and that is not laziness - it is the fix for a
 * deadlock this check caused.
 *
 * "The panel answered a request" is a good health signal and a terrible gate.
 * It was added in the same image whose HTTP server was never started, so that
 * image could never confirm itself; an unconfirmed image has a revert armed;
 * a revert armed makes slot1 un-erasable, because slot1 IS the image MCUboot
 * intends to fall back to; and an un-erasable slot1 blocks the next update.
 * So one unsatisfiable check took the update path down with it - on a board
 * whose whole recovery story is "push a new image".
 *
 * The rule this encodes: a health check may delay a confirmation, but it must
 * not be able to prevent one forever. Anything that can is a brick waiting for
 * the right bug. After the grace period the condition is waived LOUDLY, so the
 * panel being broken is still visible in `health` and in the log - it just no
 * longer strands the board.
 */
#define PMC_PANEL_GRACE_S 180

static bool chk_served(const char **detail)
{
	if (atomic_get(&s_http_200)) {
		return true;
	}

	const int64_t up = k_uptime_get() / 1000;

	if (up < PMC_PANEL_GRACE_S) {
		static char buf[56];

		snprintk(buf, sizeof(buf), "no request yet (%lld s of %d)",
			 (long long)up, PMC_PANEL_GRACE_S);
		*detail = buf;
		return false;
	}

	*detail = "WAIVED - no request in the grace period; the panel is broken";
	return true;
}

static const struct health_check checks[] = {
	{ "uptime", "a crash loop must not confirm itself", chk_uptime },
	{ "network", "the image can be updated, which is what makes a bad one "
		     "recoverable", chk_link },
	{ "expanders", "the carrier's digital I/O answers", chk_io_expanders },
	{ "analog switches", "the analog inputs are not shorted to the Ethernet "
			     "clock and the temperature bus", chk_analog_switches },
	/*
	 * Serving a request is the strongest "this image works" signal
	 * available here, and it replaces the Giga port's live-broker check.
	 * Reaching main() proves the vector table; answering a 200 on a real
	 * route proves the network stack, the server and the route table.
	 */
	{ "panel", "the web panel answered a real request", chk_served },
};

/* --- the gate ------------------------------------------------------------- */

static bool s_confirmed_by_us;
static bool s_healthy;


size_t pmc_health_check_count(void) { return ARRAY_SIZE(checks); }

const char *pmc_health_check_name(size_t i)
{
	return i < ARRAY_SIZE(checks) ? checks[i].name : "?";
}

bool pmc_health_check(size_t i, const char **detail)
{
	*detail = "";
	return i < ARRAY_SIZE(checks) ? checks[i].test(detail) : false;
}

bool pmc_health_is_healthy(void)
{
	return s_healthy;
}

static bool health_evaluate(const struct shell *sh)
{
	bool all = true;

	for (size_t i = 0; i < ARRAY_SIZE(checks); i++) {
		const char *detail = "";
		const bool ok = checks[i].test(&detail);

		if (!ok) {
			all = false;
		}
		if (sh != NULL) {
			shell_print(sh, "  %-17s %-4s %s",
				    checks[i].name, ok ? "ok" : "FAIL", detail);
		}
	}
	return all;
}

static void health_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

	for (;;) {
		if (health_evaluate(NULL)) {
			/*
			 * The indicator goes low FIRST, before confirming.
			 *
			 * Order matters: on a bench-flashed image there is
			 * nothing to confirm, so boot_write_img_confirmed()
			 * returning early must not leave the board looking
			 * unhealthy forever. Health is what D1 reports, not
			 * the state of the image trailer.
			 */
			pmc_status_set(false);
			s_healthy = true;

			if (boot_is_img_confirmed()) {
				LOG_INF("healthy; image needed no confirmation");
				return;
			}

			const int rc = boot_write_img_confirmed();

			if (rc == 0) {
				s_confirmed_by_us = true;
				LOG_INF("image confirmed: every health check passed");
				return;
			}
			LOG_ERR("health checks passed but confirming failed (%d)", rc);
		}

		k_sleep(K_SECONDS(5));
	}
}

K_THREAD_DEFINE(pmc_health_tid, 1024, health_thread, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 1000);

/* --- the report ----------------------------------------------------------- */

static int cmd_pmc_health(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "conditions for this image to confirm itself:");
	const bool all = health_evaluate(sh);

	shell_print(sh, "");
	shell_print(sh, "  %-17s %s", "verdict",
		    all ? "all conditions met" : "NOT confirming - see FAIL above");
	shell_print(sh, "  %-17s %s", "boot says",
		    boot_is_img_confirmed() ? "confirmed" :
		    "NOT confirmed - reverts on next reset");
	shell_print(sh, "  %-17s %s", "confirmed by us", s_confirmed_by_us ? "yes" : "no");
	shell_print(sh, "  %-17s %s", "DO1 indicator",
		    s_healthy ? "low - healthy" : "HIGH - not yet healthy");

	shell_print(sh, "");
	shell_print(sh, "NOT YET CHECKED, and listed so the gap is visible:");
	shell_print(sh, "  - that the temperature front ends identify themselves");
	shell_print(sh, "  - that a full channel scan completes without a fault");
	shell_print(sh, "  - that the SMP server is bound, as opposed to merely");
	shell_print(sh, "    having an address (see the comment in health.c)");

	return 0;
}

SHELL_CMD_REGISTER(health, NULL,
		   "Why this image will or will not confirm itself.",
		   cmd_pmc_health);
