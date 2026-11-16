/*
 * Bringing the network up, so that the board can be updated over it.
 *
 * SPDX-License-Identifier: MIT
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
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/net/net_event.h>

#include <zephyr/mgmt/mcumgr/transport/smp_udp.h>

#include "httpd.h"
#include <zephyr/net/hostname.h>
#include <zephyr/shell/shell.h>

LOG_MODULE_REGISTER(pmc_net, LOG_LEVEL_INF);

/*
 * Delayed, not immediate, and then RETRIED.
 *
 * The AIROC driver loads 421 KB of firmware into the CYW4343W before the
 * interface exists at all, on its own init thread, so an immediate connect
 * fails in a way that looks like bad credentials.
 *
 * AND ONE ATTEMPT IS NOT ENOUGH, which was learned the expensive way. The
 * first version fired once at three seconds and gave up on failure. Measured
 * here: after an OTA reboot the association was rejected, nothing retried, and
 * the board sat with wlan0 down and WiFi DISCONNECTED until a human typed
 * `wifi cred auto_connect` over USB. That is precisely the failure this file's
 * own header warns about - an update path that works once and then leaves the
 * board unreachable - and the header was written before the code that had it.
 *
 * The association failure itself is known and transient. The sibling Giga port
 * records it as `whd_wifi_join failed ... checkres = 3355...`, surfacing as
 * WIFI_CONNECT_RESULT status -11, with a retry succeeding ~20 s later: a board
 * that reads "wifi down" at 20 s reads "wifi up" at 25 s. So the radio is
 * fine and the fix is to keep asking.
 *
 * Backoff is bounded and then flat rather than exponential-forever. This board
 * has no other way home: if the AP is down for an hour, the right behavior is
 * to still be trying when it comes back, not to have backed off to a retry
 * every six hours.
 */
#define PMC_NET_FIRST_DELAY_MS   3000
#define PMC_NET_RETRY_MIN_MS     5000
#define PMC_NET_RETRY_MAX_MS    60000

static uint32_t s_retry_ms = PMC_NET_RETRY_MIN_MS;
static struct net_mgmt_event_callback s_wifi_cb;
static struct net_mgmt_event_callback s_ipv4_cb;
static struct k_work_delayable s_servers_work;
static bool s_servers_up;

static void net_start(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(net_start_work, net_start);

static void net_start(struct k_work *work)
{
	ARG_UNUSED(work);

	struct net_if *iface = net_if_get_first_wifi();

	if (iface == NULL) {
		LOG_WRN("no WiFi interface; nothing to auto-connect");
		return;
	}

	/*
	 * Already associated? Then this is a stray timer from before a
	 * connect completed, and re-requesting would disconnect and start
	 * over.
	 */
	if (net_if_is_up(iface)) {
		s_retry_ms = PMC_NET_RETRY_MIN_MS;
		return;
	}

	if (wifi_credentials_is_empty()) {
		/*
		 * The ordinary state of a new board, so this is information
		 * rather than a failure - and saying what to do is more
		 * useful than an error code. Not rescheduled: nothing will
		 * change until someone types the command.
		 */
		LOG_INF("no stored WiFi credentials. Add one over USB with:");
		LOG_INF("  wifi cred add -s \"<ssid>\" -k 1 -p \"<passphrase>\"");
		LOG_INF("  wifi cred auto_connect");
		LOG_INF("They persist in NVS and survive firmware updates.");
		return;
	}

	/*
	 * NET_REQUEST_WIFI_CONNECT_STORED walks every stored network in turn.
	 *
	 * This is a net_mgmt REQUEST, not a function call, and that is not a
	 * stylistic choice: there is no public wifi_credentials_connect_stored()
	 * despite CONFIG_WIFI_CREDENTIALS_CONNECT_STORED sounding like it adds
	 * one. That Kconfig adds a SHELL COMMAND, whose implementation issues
	 * exactly this request.
	 */
	const int rc = net_mgmt(NET_REQUEST_WIFI_CONNECT_STORED, iface, NULL, 0);

	if (rc != 0) {
		LOG_WRN("auto-connect request failed (%d); retrying in %u ms",
			rc, s_retry_ms);
	} else {
		LOG_INF("auto-connecting to a stored network");
	}

	/*
	 * Rescheduled either way. A request that returned 0 has only been
	 * ACCEPTED - association can still be rejected afterwards, which is
	 * the exact case that stranded the board. The check at the top makes
	 * this harmless once the link is actually up.
	 */
	k_work_reschedule(&net_start_work, K_MSEC(s_retry_ms));
	s_retry_ms = MIN(s_retry_ms * 2, PMC_NET_RETRY_MAX_MS);
}

/*
 * A disconnect re-arms the retry immediately.
 *
 * Without this, a board that loses the AP after a successful connect would
 * wait out whatever the backoff had grown to, or never retry at all once the
 * work item had stopped rescheduling.
 */
static void wifi_event(struct net_mgmt_event_callback *cb,
		       uint64_t mgmt_event, struct net_if *iface)
{
	ARG_UNUSED(cb);
	ARG_UNUSED(iface);

	if (mgmt_event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
		LOG_WRN("WiFi disconnected; re-arming auto-connect");
		s_retry_ms = PMC_NET_RETRY_MIN_MS;
		k_work_reschedule(&net_start_work, K_MSEC(PMC_NET_RETRY_MIN_MS));
	} else if (mgmt_event == NET_EVENT_WIFI_CONNECT_RESULT) {
		s_retry_ms = PMC_NET_RETRY_MIN_MS;
	}
}

/*
 * THE SERVERS START WHEN THERE IS AN ADDRESS, NOT WHEN main() RUNS.
 *
 * This is the bug that cost most of a session. zsock_bind() returns ENOENT
 * when no interface has an IPv4 address yet - even for the wildcard - so a
 * server started from main() or from a SYS_INIT binds nothing and never
 * retries. The symptom is a port that refuses every connection while the board
 * is plainly on the network, which reads like a firewall rather than a server
 * that gave up six seconds after boot. The log says it exactly once:
 *
 *     <err> smp_udp: Could not bind to receive socket (IPv4), err: 2
 *
 * at 00:00:06, in the same breath as "auto-connecting to a stored network" -
 * i.e. before DHCP could possibly have finished.
 *
 * NET_EVENT_L4_CONNECTED IS NOT THE RIGHT TRIGGER, which cost another cycle
 * to learn. It fires on ASSOCIATION, not on having an address - measured here
 * at 38 ms after "Connected" and several seconds before DHCP bound - so
 * starting on it moved the bind from "far too early" to "still too early" and
 * produced the identical ENOENT.
 *
 * NET_EVENT_IPV4_ADDR_ADD is the precondition itself rather than a proxy for
 * it: the address the bind needs now exists, by definition. It also covers a
 * static address, where waiting on DHCP specifically would not.
 *
 * Guarded by a flag because the event fires again on every reconnect and
 * http_server_start() is not idempotent.
 */
static void servers_start(struct k_work *work)
{
	ARG_UNUSED(work);

	if (s_servers_up) {
		return;
	}

	LOG_INF("address assigned; starting the panel and the update transport");
	pmc_httpd_init();

	/*
	 * SMP has the SAME bind-before-address problem, and it matters more:
	 * a board that serves a panel but cannot be updated needs a cable for
	 * every future change. Its automatic init is turned off in prj.conf so
	 * that this is the only place it is opened.
	 */
	const int rc = smp_udp_open();

	if (rc != 0) {
		/*
		 * Retried rather than given up on. An update transport that
		 * failed to open once and stays shut means every future change
		 * needs a cable - which is the single worst outcome for this
		 * board, and the one that has already happened twice.
		 */
		LOG_ERR("smp_udp_open failed (%d); retrying", rc);
		k_work_reschedule(&s_servers_work, K_SECONDS(5));
		return;
	}

	LOG_INF("update transport listening (SMP over UDP)");
	s_servers_up = true;
}

static void ipv4_event(struct net_mgmt_event_callback *cb,
		       uint64_t mgmt_event, struct net_if *iface)
{
	ARG_UNUSED(cb);
	ARG_UNUSED(iface);

	if (mgmt_event != NET_EVENT_IPV4_ADDR_ADD || s_servers_up) {
		return;
	}

	/*
	 * A short delay, not an immediate call. The event is raised from the
	 * net_mgmt thread while the address is being installed, and binding
	 * from inside that callback has been known to race the very thing it
	 * is waiting for. A second costs nothing here.
	 */
	k_work_reschedule(&s_servers_work, K_SECONDS(1));
}

static int pmc_net_init(void)
{
	k_work_init_delayable(&s_servers_work, servers_start);
	net_mgmt_init_event_callback(&s_ipv4_cb, ipv4_event, NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&s_ipv4_cb);

	net_mgmt_init_event_callback(&s_wifi_cb, wifi_event,
				     NET_EVENT_WIFI_CONNECT_RESULT |
				     NET_EVENT_WIFI_DISCONNECT_RESULT);
	net_mgmt_add_event_callback(&s_wifi_cb);

	k_work_schedule(&net_start_work, K_MSEC(PMC_NET_FIRST_DELAY_MS));
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
