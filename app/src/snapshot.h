/*
 * One pass over the hardware, taken when a page is rendered.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 *
 * WHY A SNAPSHOT AND NOT READERS CALLED FROM THE TEMPLATE. The page is
 * rendered server-side by HtmlTpl, which calls a row provider once per row -
 * and a provider that read the ADC on each call would make the three analog
 * rows three different instants, with the table silently implying they were
 * one. Worse, the panel's whole claim is "every reading on this page was taken
 * when the page was rendered". A snapshot is what makes that sentence true
 * rather than approximately true.
 *
 * It is also what keeps the two views honest. The page and /state.json render
 * from THIS struct, so they cannot disagree: there is one set of readers in
 * snapshot.c and nothing else in the application touches the ADC or the image
 * headers. A second reader for the JSON would be a second thing to keep in
 * step, and the first time they drifted the panel and the tooling would
 * describe different boards.
 */
#ifndef PMC_SNAPSHOT_H
#define PMC_SNAPSHOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "clock.h"
#include "expander.h"

#define PMC_SNAP_AI   3
#define PMC_SNAP_SLOT 2

struct pmc_ai_read {
	const char *name;
	const char *pad;
	int16_t     raw;
	int32_t     mv;
	bool        open; /* the SYSCFG analog switch is OPEN, i.e. the pad is
			   * not tied to the Ethernet clock or SPI2 - see
			   * boards/shields/.../pmc_syscfg.c */
	bool        ok;   /* the read succeeded; raw and mv are meaningless if
			   * this is false, and are reported as -1 */
};

struct pmc_slot_read {
	uint8_t slot;
	char    version[24];
	bool    present;
	bool    active;
	bool    confirmed;
};

/*
 * WHETHER THERE IS A CLOCK, AND WHY NOT.
 *
 * The carrier's PCF8563 reports its own trustworthiness and that is better
 * than anything this application could infer. The VL bit in its seconds
 * register means the oscillator stopped, and the driver turns that into
 * -ENODATA from rtc_get_time(). A 100 mF supercapacitor holds the part up for
 * at least 48 hours with board power removed (datasheet section 3-20), so
 * these three states are the difference between switching the bench on
 * overnight and switching it on after a long weekend.
 *
 * ABSENT and LOST must not render the same, for the same reason a channel
 * with no reader must not render as LOW: one is a missing part and the other
 * is a part saying it lost time.
 */
enum pmc_clock_state {
	PMC_CLOCK_ABSENT = 0, /* no device in the tree, or it never answered */
	PMC_CLOCK_LOST,       /* answered -ENODATA: the oscillator had stopped */
	PMC_CLOCK_HELD,       /* read cleanly: the supercapacitor carried it */
};

/*
 * WHAT THE RADIO SAYS ABOUT THE LINK IT IS ON.
 *
 * Sampled on a workqueue by net.c and cached, NOT read while a page renders:
 * the underlying call is an IOCTL over SDIO to the radio, and the panel is
 * served by one thread. age_ms is therefore published beside the values, so a
 * stale sample cannot be read as a current one. -1 means no sample has ever
 * succeeded, which is not the same as a reading of zero.
 */
struct pmc_link {
	int32_t  age_ms;       /* ms since the sample; -1 = never sampled */
	int32_t  rssi;         /* dBm, as the radio reports it */
	uint16_t chan;
	uint16_t rate_tenths;  /* PHY rate x10, in the driver's own units */
	uint8_t  mode;         /* enum wifi_link_mode */
	uint8_t  state;        /* enum wifi_iface_state */
	int16_t  rc;           /* net_mgmt() return of the last attempt */
};

/*
 * AND THE SAME FOR THE WIRE.
 *
 * The Ethernet analogue of struct pmc_link, sampled by the same poller. A
 * PHY has no RSSI, so what stands in for it is the negotiated speed and
 * duplex: an unexpected 10BASE-T half duplex says as much about a bad cable
 * as a poor RSSI says about a bad antenna.
 *
 * carrier and link are kept APART on purpose. carrier is what the MAC driver
 * believes; link is what the PHY reports through MDIO. They disagree exactly
 * when something interesting is wrong, and collapsing them into one "up"
 * would hide it.
 *
 * NO ADDRESS AND NO HARDWARE ADDRESS HERE, deliberately - has_addr is a
 * boolean. This panel is screenshotted into a public repository, and an
 * address is the one thing in a screenshot that no text scan can catch.
 */
struct pmc_eth {
	int32_t  age_ms;       /* ms since the sample; -1 = never sampled */
	uint8_t  present;      /* the interface exists in this build at all */
	uint8_t  carrier;      /* net_if_is_carrier_ok() - the MAC's view */
	uint8_t  link_up;      /* the PHY's view, over MDIO */
	uint8_t  full_duplex;
	uint16_t speed_mbit;   /* 10, 100, 1000; 0 = not reported */
	uint8_t  has_addr;     /* an IPv4 address is bound. NOT the address. */
	int16_t  rc;           /* phy_get_link_state() return */
	int16_t  init_res;     /* the MAC driver's OWN init errno, negated */

	/* Raw registers, read only when the MAC failed init. docs/MEASURED.md */
	uint8_t  probed;       /* the probe ran; without this 0 is ambiguous */
	uint8_t  gates;        /* RCC AHB1ENR: b0 MAC, b1 TX, b2 RX */
	uint8_t  swr;          /* ETH DMAMR SWR still set = reset never finished */
	uint8_t  epis;         /* SYSCFG PMCR EPIS_SEL: 0 = MII, 4 = RMII */
	uint8_t  pmcr_sw;      /* PMCR switches: b0 PA0 b1 PA1 b2 PC2 b3 PC3 */
	uint8_t  phy_rst;      /* PJ15, the PHY reset-release line, read back */
	int16_t  phy_addr;     /* MDIO address that answered; -1 = none of 32 */
	uint16_t phy_id1;      /* MDIO register 2 */
	uint16_t phy_id2;      /* MDIO register 3 */
};

/*
 * The analog trend. `points` is an SVG polyline attribute built from a ring
 * sampled on its own timer - see snapshot.c on why it is not derived at render
 * time. Y is fixed at 0..10 V; a failed read leaves a gap in the line.
 */
size_t pmc_snapshot_ai_points(size_t ch, char *out, size_t cap);

/* Seconds the trend currently spans; 0 before two samples exist. */
uint32_t pmc_snapshot_ai_span_s(void);

struct pmc_snapshot {
	const char *host;
	const char *fw;
	uint32_t    uptime_s;

	struct pmc_link link;  /* the radio link; see struct pmc_link */
	struct pmc_eth  eth;   /* the wire; see struct pmc_eth */

	uint8_t clock_state;  /* enum pmc_clock_state */
	int64_t clock_epoch;  /* UTC seconds; 0 unless clock_state is HELD */
	struct pmc_clock_drift drift;  /* what NTP has measured, if anything */
	struct pmc_io_state    io;     /* DIN and PDIO off the expanders */
	struct pmc_rail        rail;   /* 24 V, inferred - never measured */

	/* The DO logic lines, off the GPIO registers; -1 = port not ready. */
	int8_t                 do_out[8];

	/* ms since the last RS485 byte each way; -1 = never. Drives the lamps. */
	int32_t                rs485_tx_age;
	int32_t                rs485_rx_age;

	struct pmc_ai_read   ai[PMC_SNAP_AI];
	size_t               n_ai;
	struct pmc_slot_read slot[PMC_SNAP_SLOT];
	size_t               n_slot;
};

/* From shell_mcuboot.c - the swap state, which the panel is now the only
   way to see. */
const char *pmc_swap_type_name(void);
bool pmc_img_confirmed(void);

/* Reads everything once. Call it before rendering, never during. */
void pmc_snapshot_take(struct pmc_snapshot *s);

/*
 * The reading for one named channel, written into `out`.
 *
 * Returns the logic level where there is one: 1 high, 0 low, -1 unknown. Only
 * the analog channels answer anything today; DIN, DO and PDIO are TCA6424A
 * bits that nothing reads until M2, so they report unknown and `out` says so
 * in words. THEY DO NOT REPORT LOW. A channel with no reader and a channel
 * reading 0 V must not render identically, because the second is a
 * measurement and the first is an absence.
 */
int pmc_snapshot_reading(const struct pmc_snapshot *s, const char *chan,
			 char *out, size_t cap);

/* Serializes the snapshot as JSON. Returns the byte count written. */
size_t pmc_snapshot_json(const struct pmc_snapshot *s, char *out, size_t cap);

#endif /* PMC_SNAPSHOT_H */
