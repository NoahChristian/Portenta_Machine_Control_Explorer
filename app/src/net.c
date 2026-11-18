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

/* For the Ethernet raw-register probe below. */
#include <stm32_ll_system.h>

/* Under the Zephyr API on purpose - see the power save note below. */
#include <whd_wifi_api.h>
#include <whd_types.h>
#include <zephyr/net/wifi_credentials.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/net/net_event.h>

#include <zephyr/mgmt/mcumgr/transport/smp_udp.h>

#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/dns_resolve.h>
#include <zephyr/net/phy.h>

#include "clock.h"
#include "snapshot.h"
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
/*
 * WIFI POWER SAVE, AND WHY net_mgmt CANNOT DO IT HERE.
 *
 * The radio wakes only on DTIM beacons once PM2 is on. Outbound traffic is
 * unaffected, so a board that answers happily can still be slow to reach, and
 * a large INBOUND transfer stalls - which is exactly how this presented: a
 * megabyte of OTA repeatedly dying part-way while small requests always
 * worked, and a first HTTP request after an idle moment taking 1467 ms
 * against 63-102 ms for the same document once the link was awake.
 *
 * THE OBVIOUS FIX IS A NO-OP ON THIS DRIVER. Zephyr defines
 * NET_REQUEST_WIFI_PS, and calling it looks right and compiles, but the AIROC
 * driver's wifi_mgmt_ops table has no .set_power_save and no
 * .get_power_save - it offers scan, connect, disconnect, ap_enable,
 * ap_disable, iface_status and get_stats, and nothing else. The request
 * therefore returns -ENOTSUP and changes nothing. With no console on this
 * board the warning that said so was never seen, and the call sat there
 * looking like a fix for an entire version.
 *
 * So this goes under the Zephyr API to WHD itself, which is where the setting
 * actually lives. whd_management.c enables PM2 unconditionally on radio
 * bring-up:
 *
 *     whd_wifi_enable_powersave_with_throughput(ifp, DEFAULT_PM2_SLEEP_RET_TIME);
 *
 * with DEFAULT_PM2_SLEEP_RET_TIME 200. A sibling project on this bench found
 * the identical call, with the identical 200 ms, by disassembling a
 * completely different vendor stack for the same radio - and recorded that
 * whether it applied under Zephyr had never been measured and must not be
 * assumed. It applies: same WHD sources, same constant, same unconditional
 * else branch.
 *
 * THE MODE IS READ BACK, not assumed. That discipline is borrowed wholesale
 * from the same place, and it is the only reason the net_mgmt version above
 * was caught: a fix you cannot observe is a fix you are only hoping for. The
 * value is published in the state document so it can be checked from a
 * browser rather than from a console this board does not have.
 *
 * Call it AFTER the join. Powering the radio up re-arms PM2, so anything that
 * cycles the radio undoes this.
 */
whd_interface_t airoc_wifi_get_whd_interface(void);

static uint32_t s_wifi_ps_mode = 99;   /* 99 = never read */

uint32_t pmc_net_wifi_ps_mode(void)
{
	return s_wifi_ps_mode;
}

static void pmc_net_disable_wifi_ps(void)
{
	whd_interface_t ifp = airoc_wifi_get_whd_interface();
	uint32_t before = 99, after = 99;

	if (ifp == NULL) {
		LOG_WRN("WiFi PS: no WHD interface, left alone");
		return;
	}

	(void)whd_wifi_get_powersave_mode(ifp, &before);
	const uint32_t rc = whd_wifi_disable_powersave(ifp);

	(void)whd_wifi_get_powersave_mode(ifp, &after);
	s_wifi_ps_mode = after;

	LOG_INF("WiFi PS: mode %u -> %u (rc %u) - %s",
		(unsigned)before, (unsigned)after, (unsigned)rc,
		(after == NO_POWERSAVE_MODE) ? "powersave OFF" : "STILL ON");
}

/*
 * THE LINK ITSELF, WHICH THIS PANEL SHOULD HAVE PUBLISHED FROM THE START.
 *
 * It did not, and the omission cost real time. A stalled OTA was explained
 * five different ways from software - processor load, power save, the panel's
 * own polling, the receive pool, and the radio's access-category queues -
 * while the signal strength, the negotiated rate and the channel were on no
 * page and in no log. Every other instrument on this bench reports them in
 * its connection details.
 *
 * Measured on 2026-11-17, from the bench PC and needing nothing from this
 * firmware: 0% loss on 32-byte frames and 73% on 1000-byte frames, while
 * another board on the same access point in the same minute lost none. Loss
 * that rises with frame length is a property of the radio link, and it is
 * invisible to every counter the application had - a frame lost on the air
 * never reaches the driver, so no buffer fails to allocate and no queue
 * overflows. rxDrops and txErrs both read zero throughout.
 *
 * Sampled on the workqueue rather than during a render, because
 * whd_wifi_get_ap_info() is an IOCTL over SDIO and the panel has ONE server
 * thread. The sample's age is published with it.
 */
#define LINK_POLL_MS 2000

static void link_poll(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(link_poll_work, link_poll);

/* Defined below, next to the rest of the Ethernet handling, because it has
 * two bodies - one real and one empty - depending on whether &mac is enabled.
 * The poller calls it, and the poller comes first. */
static void eth_sample(void);

static struct pmc_link s_link = {
	.age_ms = -1,
	.rc = -EAGAIN,
};
static int64_t s_link_stamp;   /* 0 = no sample has ever succeeded */

static void link_poll(struct k_work *work)
{
	struct net_if *iface = net_if_get_first_wifi();
	struct wifi_iface_status st = { 0 };
	int rc;

	ARG_UNUSED(work);

	if (iface == NULL) {
		s_link.rc = -ENODEV;
		goto again;
	}

	rc = net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &st, sizeof(st));
	s_link.rc = (int16_t)rc;

	if (rc == 0) {
		s_link.rssi = (int32_t)st.rssi;
		s_link.chan = (uint16_t)st.channel;
		/*
		 * The driver hands this over as a float and the units are its
		 * own - it compares the same value against 6, 9, 12 as if
		 * megabits. Published x10 as an integer, without reinterpreting
		 * it, because a number passed through unchanged can be checked
		 * against the datasheet later and a rescaled one cannot. No
		 * float ever reaches snprintk, which would need
		 * CBPRINTF_FP_SUPPORT to print it at all.
		 */
		s_link.rate_tenths = (uint16_t)(st.current_phy_tx_rate * 10.0f);
		s_link.mode = (uint8_t)st.link_mode;
		s_link.state = (uint8_t)st.state;
		s_link_stamp = k_uptime_get();
	}

again:
	eth_sample();
	k_work_reschedule(&link_poll_work, K_MSEC(LINK_POLL_MS));
}

void pmc_net_link_read(struct pmc_link *out)
{
	*out = s_link;
	out->age_ms = (s_link_stamp == 0)
			      ? -1
			      : (int32_t)(k_uptime_get() - s_link_stamp);
}

/*
 * THE WIRE, WHICH EXISTS SO THE UPDATE PATH DOES NOT DEPEND ON THE RADIO.
 *
 * Compiled out cleanly when &mac is disabled, so re-disabling it in the
 * shield overlay still builds and the panel then reports present=0 rather
 * than a plausible set of zeros.
 *
 * THE INTERFACE CANNOT BE FOUND BY L2 TYPE. In Zephyr a WiFi interface is
 * also an ETHERNET L2 interface, so net_if_get_first_by_type(ETHERNET) can
 * and does return the radio - which would have this code reporting the PHY
 * state of a device that has no PHY. Looked up by its devicetree node
 * instead, which is unambiguous by construction.
 */
#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(mac))

#define PMC_HAVE_ETH 1

static const struct device *const s_eth_dev = DEVICE_DT_GET(DT_NODELABEL(mac));
static const struct device *const s_eth_phy =
	DEVICE_DT_GET(DT_PHANDLE(DT_NODELABEL(mac), phy_handle));

static struct pmc_eth s_eth = { .age_ms = -1, .present = 1, .phy_addr = -1 };
static int64_t s_eth_stamp;
static bool s_eth_dhcp_on;

/*
 * One MDIO read, driven directly, because the driver's path is unavailable
 * when its init failed. HAL_ETH_Init leaves MACMDIOAR.CR at reset if it bails
 * early, so the divider is set here or MDC runs at HCLK/42. docs/MEASURED.md.
 */
static int eth_mdio_raw(uint8_t prtad, uint8_t regad, uint16_t *out)
{
	uint32_t reg = ETH->MACMDIOAR;
	int64_t deadline;

	if (reg & ETH_MACMDIOAR_MB) {
		return -EBUSY;
	}

	reg &= ~(ETH_MACMDIOAR_PA | ETH_MACMDIOAR_RDA | ETH_MACMDIOAR_MOC |
		 ETH_MACMDIOAR_CR);
	reg |= ETH_MACMDIOAR_CR_DIV102;
	reg |= ((uint32_t)prtad << 21) & ETH_MACMDIOAR_PA;
	reg |= ((uint32_t)regad << 16) & ETH_MACMDIOAR_RDA;
	reg |= ETH_MACMDIOAR_MOC_RD | ETH_MACMDIOAR_MB;

	ETH->MACMDIOAR = reg;

	deadline = k_uptime_get() + 50;
	while (ETH->MACMDIOAR & ETH_MACMDIOAR_MB) {
		if (k_uptime_get() > deadline) {
			return -ETIMEDOUT;
		}
	}

	*out = (uint16_t)ETH->MACMDIODR;
	return 0;
}

/*
 * The PHY's own link state, over MDIO, for when the MAC is dead and
 * phy_get_link_state() therefore cannot be asked. IEEE-standard registers
 * only - no vendor datasheet - so this holds for whatever PHY answers.
 *
 * BMSR.LINK IS LATCHING LOW: it reports 0 if the link dropped since the last
 * read, even when the link is back. So it is read TWICE and the second read is
 * the current state. A single read makes a recovered link look down.
 */
#define MII_BMSR     1U
#define MII_ANAR     4U
#define MII_ANLPAR   5U
#define BMSR_LINK    BIT(2)

static void eth_mdio_link(uint8_t addr)
{
	uint16_t bmsr = 0, anar = 0, anlpar = 0, common;

	(void)eth_mdio_raw(addr, MII_BMSR, &bmsr);
	if (eth_mdio_raw(addr, MII_BMSR, &bmsr) != 0) {
		return;
	}

	s_eth.link_up = (bmsr & BMSR_LINK) ? 1U : 0U;
	if (!s_eth.link_up) {
		s_eth.speed_mbit = 0U;
		s_eth.full_duplex = 0U;
		return;
	}

	if (eth_mdio_raw(addr, MII_ANAR, &anar) != 0 ||
	    eth_mdio_raw(addr, MII_ANLPAR, &anlpar) != 0) {
		return;
	}

	/* The best mode BOTH sides advertise is the negotiated one. */
	common = anar & anlpar;

	if (common & BIT(8)) {
		s_eth.speed_mbit = 100U;
		s_eth.full_duplex = 1U;
	} else if (common & BIT(7)) {
		s_eth.speed_mbit = 100U;
		s_eth.full_duplex = 0U;
	} else if (common & BIT(6)) {
		s_eth.speed_mbit = 10U;
		s_eth.full_duplex = 1U;
	} else if (common & BIT(5)) {
		s_eth.speed_mbit = 10U;
		s_eth.full_duplex = 0U;
	}
}

/*
 * Why these five registers, and why the sweep is conditional on SWR:
 * docs/MEASURED.md. Safe only because the MAC's init failed, so no driver
 * owns any of this.
 */
static void eth_probe_registers(void)
{
	const uint32_t ahb1 = RCC->AHB1ENR;
	uint8_t gates = 0;

	if (ahb1 & RCC_AHB1ENR_ETH1MACEN) {
		gates |= BIT(0);
	}
	if (ahb1 & RCC_AHB1ENR_ETH1TXEN) {
		gates |= BIT(1);
	}
	if (ahb1 & RCC_AHB1ENR_ETH1RXEN) {
		gates |= BIT(2);
	}

	s_eth.gates = gates;
	s_eth.epis = (uint8_t)((SYSCFG->PMCR & SYSCFG_PMCR_EPIS_SEL) >>
			       SYSCFG_PMCR_EPIS_SEL_Pos);

	/* The analog switches as the RUNNING board has them, not as built. */
	s_eth.pmcr_sw = (uint8_t)((SYSCFG->PMCR >> SYSCFG_PMCR_PA0SO_Pos) & 0xfU);

	s_eth.phy_addr = -1;
	s_eth.probed = 1U;

	if (RCC->AHB4ENR & RCC_AHB4ENR_GPIOJEN) {
		s_eth.phy_rst = (GPIOJ->ODR & BIT(15)) ? 1U : 0U;
	}

	/* Reading ETH with its gate off is a bus fault, not an error code. */
	if ((gates & BIT(0)) == 0U) {
		LOG_ERR("eth probe: MAC clock gate is off; ETH not read");
		return;
	}

	s_eth.swr = (ETH->DMAMR & ETH_DMAMR_SWR) ? 1U : 0U;

	/*
	 * SWR SET MAKES A *SILENT* SWEEP UNINFORMATIVE - it resets the MAC's
	 * own logic, MDIO included - so swr is published beside the result
	 * rather than the sweep being skipped. An answer still proves the PHY;
	 * only a non-answer is ambiguous.
	 */
	if (s_eth.swr) {
		LOG_WRN("eth probe: DMAMR SWR set; a silent sweep proves nothing");
	}

	for (uint8_t a = 0U; a < 32U; a++) {
		uint16_t id1 = 0, id2 = 0;

		if (eth_mdio_raw(a, 2U, &id1) != 0 ||
		    eth_mdio_raw(a, 3U, &id2) != 0) {
			continue;
		}
		if (id1 == 0xffffU || (id1 == 0U && id2 == 0U)) {
			continue;
		}

		s_eth.phy_addr = (int16_t)a;
		s_eth.phy_id1 = id1;
		s_eth.phy_id2 = id2;
		break;
	}

	LOG_INF("eth probe: gates 0x%02x epis %u phyRst %u swr %u addr %d id %04x:%04x",
		s_eth.gates, s_eth.epis, s_eth.phy_rst, s_eth.swr,
		(int)s_eth.phy_addr, s_eth.phy_id1, s_eth.phy_id2);
}

static void eth_sample(void)
{
	struct net_if *iface;
	struct phy_link_state st = { 0 };
	int rc;

	/*
	 * THREE DISTINCT FAULTS, THREE DISTINCT CODES. They were all -ENODEV
	 * once, and that cost a round of guessing: the panel said rc -19 and
	 * there was no way to tell "the driver never initialized" from "it
	 * initialized but has no net_if" from "the PHY is not answering". With
	 * no console on this board the rc IS the diagnostic, so it has to
	 * carry which one it was.
	 *
	 *   -ENODEV  the MAC driver is not ready - its init failed. Check the
	 *            init priorities against the PHY reset regulator; see the
	 *            block about this in app/prj.conf.
	 *   -ENXIO   the driver is ready but has no interface bound.
	 *   -ENOENT  the MAC is fine and the PHY device is not ready.
	 */
	if (!device_is_ready(s_eth_dev)) {
		s_eth.rc = -ENODEV;

		/*
		 * WHY THE DRIVER FAILED, FROM THE DRIVER, rather than guessed.
		 *
		 * Zephyr keeps every device's init return value in
		 * dev->state->init_res as a POSITIVE errno (the comment on the
		 * field says so: init functions return negative, and since no
		 * Zephyr errno exceeds 255 the positive value is stored in a
		 * uint8_t). Negated here so it reads like an errno.
		 *
		 * This board has no console and no RTT, so a driver that fails
		 * at init normally produces no evidence at all - which is how
		 * this one consumed a round of hypotheses: a PHY still in reset,
		 * a missing RMII reference clock, the SYSCFG interface-select
		 * bits, a PHY needing a post-reset settling delay. All plausible,
		 * none measured. The driver knew the answer the whole time.
		 */
		if (s_eth_dev->state != NULL) {
			s_eth.init_res = -(int16_t)s_eth_dev->state->init_res;
		}

		/*
		 * -EIO IS AMBIGUOUS. eth_stm32_hal_common.c returns it both for
		 * a clock-setup failure (:102) and for HAL_ETH_Init failing
		 * (:123). Only the registers separate the two.
		 */
		if (!s_eth.probed) {
			eth_probe_registers();
		}

		/*
		 * The PHY still answers, so the panel reports the WIRE's state
		 * even though the MAC cannot carry it. Sampled every pass, not
		 * once, because a cable can come and go.
		 */
		if (s_eth.phy_addr >= 0) {
			eth_mdio_link((uint8_t)s_eth.phy_addr);
			s_eth_stamp = k_uptime_get();
		}
		return;
	}

	iface = net_if_lookup_by_dev(s_eth_dev);
	if (iface == NULL) {
		s_eth.rc = -ENXIO;
		return;
	}

	s_eth.carrier = net_if_is_carrier_ok(iface) ? 1U : 0U;
	s_eth.has_addr =
		(net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED) != NULL)
			? 1U
			: 0U;

	if (device_is_ready(s_eth_phy)) {
		rc = phy_get_link_state(s_eth_phy, &st);
		s_eth.rc = (int16_t)rc;
		if (rc == 0) {
			s_eth.link_up = st.is_up ? 1U : 0U;
			s_eth.full_duplex =
				PHY_LINK_IS_FULL_DUPLEX(st.speed) ? 1U : 0U;
			if (PHY_LINK_IS_SPEED_1000M(st.speed)) {
				s_eth.speed_mbit = 1000U;
			} else if (PHY_LINK_IS_SPEED_100M(st.speed)) {
				s_eth.speed_mbit = 100U;
			} else if (PHY_LINK_IS_SPEED_10M(st.speed)) {
				s_eth.speed_mbit = 10U;
			} else {
				s_eth.speed_mbit = 0U;
			}
		}
	} else {
		s_eth.rc = -ENOENT;
	}

	s_eth_stamp = k_uptime_get();

	/*
	 * DHCP IS STARTED HERE, EXPLICITLY, because nothing else in this image
	 * starts it. CONFIG_NET_CONFIG_SETTINGS is off, so Zephyr's net_config
	 * never runs, and the AIROC driver - unlike the esp32 and ameba ones -
	 * does not call net_dhcpv4_start() either. Whatever gets the radio its
	 * lease, relying on the same unidentified path for a second interface
	 * would be guessing.
	 *
	 * Bound to the carrier rather than done once at boot, and re-armed on
	 * unplug, so plugging a cable in later works without a reset - which is
	 * the whole point of having this as a fallback.
	 */
	if (s_eth.carrier && !s_eth_dhcp_on) {
		LOG_INF("eth: carrier up, starting DHCP");
		net_dhcpv4_start(iface);
		s_eth_dhcp_on = true;
	} else if (!s_eth.carrier && s_eth_dhcp_on) {
		LOG_INF("eth: carrier lost, stopping DHCP");
		net_dhcpv4_stop(iface);
		s_eth_dhcp_on = false;
	}
}

void pmc_net_eth_read(struct pmc_eth *out)
{
	*out = s_eth;
	out->age_ms = (s_eth_stamp == 0)
			      ? -1
			      : (int32_t)(k_uptime_get() - s_eth_stamp);
}

#else /* &mac disabled */

#define PMC_HAVE_ETH 0

static void eth_sample(void) { }

void pmc_net_eth_read(struct pmc_eth *out)
{
	static const struct pmc_eth absent = { .age_ms = -1, .present = 0,
					       .phy_addr = -1 };

	*out = absent;
}

#endif

/*
 * The addresses, as strings, for the panel's network window.
 *
 * Read live rather than cached: these change on a DHCP renew and a stale
 * gateway is worse than none - it sends someone to check a router that was
 * never the one in use. The cost is a few net_if reads per render, which is
 * nothing next to the ADC work already done there.
 */
static void ip_fill(struct net_if *iface, struct pmc_ipinfo *out)
{
	struct net_in_addr *a;
	struct net_in_addr m, g;

	memset(out, 0, sizeof(*out));
	if (iface == NULL) {
		return;
	}

	a = net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);
	if (a == NULL) {
		return;
	}

	out->have = 1U;
	(void)net_addr_ntop(NET_AF_INET, a, out->ip, sizeof(out->ip));

	m = net_if_ipv4_get_netmask_by_addr(iface, a);
	(void)net_addr_ntop(NET_AF_INET, &m, out->mask, sizeof(out->mask));

	g = net_if_ipv4_get_gw(iface);
	(void)net_addr_ntop(NET_AF_INET, &g, out->gw, sizeof(out->gw));
}

void pmc_net_info_read(struct pmc_netinfo *out)
{
	struct dns_resolve_context *ctx;

	memset(out, 0, sizeof(*out));

	ip_fill(net_if_get_first_wifi(), &out->wifi);
#if PMC_HAVE_ETH
	ip_fill(device_is_ready(s_eth_dev) ? net_if_lookup_by_dev(s_eth_dev)
					   : NULL,
		&out->eth);
#endif

	/*
	 * DNS is resolver-wide, not per interface. mDNS and LLMNR entries are
	 * skipped: they are link-local responders this image registers itself,
	 * not servers anybody configured, and listing them as "DNS" would send
	 * a reader looking for a resolver that does not exist.
	 */
	ctx = dns_resolve_get_default();
	if (ctx == NULL) {
		return;
	}

	for (size_t i = 0; i < ARRAY_SIZE(ctx->servers) &&
			   out->n_dns < ARRAY_SIZE(out->dns); i++) {
		const struct net_sockaddr *sa = &ctx->servers[i].dns_server;

		if (sa->sa_family != NET_AF_INET || ctx->servers[i].is_mdns ||
		    ctx->servers[i].is_llmnr) {
			continue;
		}

		(void)net_addr_ntop(NET_AF_INET,
				    &net_sin(sa)->sin_addr,
				    out->dns[out->n_dns],
				    sizeof(out->dns[0]));
		out->n_dns++;
	}
}

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

		/*
		 * POWER SAVE OFF.
		 *
		 * Measured on this board before this existed: the first HTTP
		 * request after an idle moment took 1467 ms and the next three
		 * took 308, 315 and 455 - for 1.3 kB of JSON, on a 480 MHz M7.
		 * That shape is a radio asleep between beacons, not a busy
		 * processor, and the few-hundred-millisecond floor under every
		 * request is the beacon interval showing through. It was
		 * nearly misdiagnosed as the board being short of horsepower,
		 * which it conspicuously is not.
		 *
		 * It is also the likeliest reason a megabyte would not upload.
		 * A stream of SMP writes needs the receiver awake
		 * continuously; with power save on, the gaps between wakeups
		 * are where requests go to die - which presents exactly as
		 * "the receiver is not ready", because that is what it is.
		 *
		 * This is a mains-powered bench instrument fed from 24 V.
		 * There is nothing to save here and a great deal to lose.
		 */
		pmc_net_disable_wifi_ps();

		/*
		 * Start sampling the link. Rescheduled rather than scheduled,
		 * because this event fires again on every reconnect and a second
		 * k_work_schedule() on a pending item would be ignored.
		 */
		k_work_reschedule(&link_poll_work, K_MSEC(500));
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
	 * Only gives a semaphore. The sync itself blocks on a socket for
	 * seconds and must not do that here - this is the system workqueue, and
	 * what happens when something heavy runs on it is recorded in httpd.c.
	 */
	pmc_clock_net_up();

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
