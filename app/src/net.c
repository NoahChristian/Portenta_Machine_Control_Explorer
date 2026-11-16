/*
 * Bringing the network up, so that the board can be updated over it.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Written by Noah Christian (C) 2026
 *
 * CREDENTIALS ARE NOT IN THIS REPOSITORY, AND NOT IN THE IMAGE EITHER.
 *
 * They are typed once at the shell and stored in the `kvs` NVS partition by
 * Zephyr's wifi_credentials library:
 *
 *     wifi cred add -s "<ssid>" -k 1 -p "<passphrase>"
 *     wifi cred auto_connect
 *
 * That is a deliberate choice over the usual app_secrets.h arrangement, and it
 * is better in three ways. A credential never enters the build, so it cannot
 * reach a repository through a header, an .example file copied from a working
 * one, or a .bin committed by accident - which is how credentials have leaked
 * on this bench before. It survives a firmware update, because NVS is outside
 * both image slots. And changing networks needs no rebuild and no reflash.
 *
 * The cost is that a factory-fresh board has no credentials and must be given
 * them over USB once. That is the right trade for a board that is going to be
 * updated over the air for the rest of its life.
 *
 * WHY AUTO-CONNECT AT BOOT MATTERS MORE THAN IT LOOKS. After an OTA the board
 * resets. If it does not rejoin the network by itself, the update path is
 * one-shot: the first update works and the board is then unreachable. So this
 * is part of the update mechanism, not a convenience.
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/wifi_credentials.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/hostname.h>
#include <zephyr/shell/shell.h>

LOG_MODULE_REGISTER(pmc_net, LOG_LEVEL_INF);

/*
 * Delayed, not immediate.
 *
 * The AIROC driver has to load 421 KB of firmware into the CYW4343W before the
 * interface exists at all, and that happens on its own init thread. Calling
 * connect before then fails in a way that looks like bad credentials.
 */
#define PMC_NET_START_DELAY_MS 3000

static void net_start(struct k_work *work)
{
	ARG_UNUSED(work);

	struct net_if *iface = net_if_get_first_wifi();

	if (iface == NULL) {
		LOG_WRN("no WiFi interface; nothing to auto-connect");
		return;
	}

	/*
	 * NET_REQUEST_WIFI_CONNECT_STORED walks every stored network in turn.
	 *
	 * This is a net_mgmt REQUEST, not a function call, and that is not a
	 * stylistic choice: there is no public wifi_credentials_connect_stored()
	 * despite CONFIG_WIFI_CREDENTIALS_CONNECT_STORED sounding like it adds
	 * one. That Kconfig adds a SHELL COMMAND, whose implementation
	 * (wifi_credentials_shell.c:380) issues exactly this request. Calling
	 * the plausible-looking function name fails to compile with a suggestion
	 * to use wifi_credentials_for_each_ssid instead, which would be the
	 * long way round to the same place.
	 */
	if (wifi_credentials_is_empty()) {
		LOG_INF("no stored WiFi credentials. Add one over USB with:");
		LOG_INF("  wifi cred add -s \"<ssid>\" -k 1 -p \"<passphrase>\"");
		LOG_INF("  wifi cred auto_connect");
		LOG_INF("They persist in NVS and survive firmware updates.");
		return;
	}

	const int rc = net_mgmt(NET_REQUEST_WIFI_CONNECT_STORED, iface, NULL, 0);

	if (rc != 0) {
		LOG_WRN("auto-connect from stored credentials failed (%d)", rc);
		return;
	}

	LOG_INF("auto-connecting to a stored network");
}

static K_WORK_DELAYABLE_DEFINE(net_start_work, net_start);

static int pmc_net_init(void)
{
	k_work_schedule(&net_start_work, K_MSEC(PMC_NET_START_DELAY_MS));
	return 0;
}

SYS_INIT(pmc_net_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

/*
 * `mdnsname` - what name this image actually answers to.
 *
 * A Kconfig string and the name on a label are two different things, and only
 * one of them is compiled in. This reads back the built value rather than
 * repeating the intended one, so a board that was flashed with the wrong
 * image says so instead of being quietly unreachable by the name on its case.
 */
static int cmd_mdnsname(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "hostname   %s", net_hostname_get());
	shell_print(sh, "resolves   %s.local  (mDNS names are case-insensitive)",
		    net_hostname_get());
	shell_print(sh, "");
	shell_print(sh, "No _http._tcp service record is advertised yet - there is");
	shell_print(sh, "no web panel to point it at. It is registered alongside the");
	shell_print(sh, "first real route, not before, because advertising a port");
	shell_print(sh, "nothing listens on is worse than advertising nothing.");
	return 0;
}

SHELL_CMD_REGISTER(mdnsname, NULL, "The mDNS name this image answers to.",
		   cmd_mdnsname);
