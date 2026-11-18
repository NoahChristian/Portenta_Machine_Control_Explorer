/*
 * The variable registry: every name web/index.html is allowed to substitute.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 *
 * THIS IS THE ONE C++ TRANSLATION UNIT IN THE APPLICATION. The rest is C; the
 * HtmlTpl engine is C++ because its registry is a flat const table whose rows
 * deduce their own type from decltype, which is the whole reason a misdeclared
 * variable is a compile error here instead of four bytes of an int read as a
 * float. So the registry and the render live in C++, and everything that
 * reaches the hardware stays in C behind extern "C" entry points.
 *
 * WHY A REGISTRY AND NOT A HANDLER THAT PRINTS. The page names what it wants
 * and this table says what each name is. tools/mkhtml.py reads both and fails
 * the build on a name that is not here - so the failure mode of a renamed
 * variable is a build error naming the line, not a page that renders with a
 * plausible-looking empty cell where a reading should be. That check is the
 * reason the panel can be trusted to be either right or broken, and not
 * quietly half-right.
 *
 * NO MARKUP IS BUILT HERE. The strings this file formats are values -
 * "19 raw, 15 mV", "translate(64.5 81.0)", "term k-din unverified". The class
 * names and the transform are attribute VALUES that the template places
 * through data-tpl-attr; the shape of the page is in web/index.html and
 * nowhere else.
 *
 * ROW VARIABLES ARE SHARED GLOBALS, which is safe because exactly one render
 * runs at a time - Zephyr has one HTTP server thread, and the adapter takes a
 * lock besides. See HtmlTplServerZephyr.h.
 */

#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <HtmlTplEngine.h>
#include <HtmlTplServerZephyr.h>
#include <HtmlTplVars.h>

extern "C" {
#include "health.h"
#include "page_vars.h"
#include "snapshot.h"
}

#include "web_gen/page_index.h"

LOG_MODULE_REGISTER(pmc_page, LOG_LEVEL_INF);

/* ------------------------------------------------------------ the board map */

/*
 * The geometry table, solved by tools/gen_board.py from tools/terminals.csv.
 * Coordinates and names, no markup - see board_table.inc's own header.
 */
struct pmc_term {
	float       x, y;
	bool        top;
	const char *label;
	const char *chan;
	const char *kind;
	const char *conf;
	const char *block;
	bool        led;
	/* What the channel IS in silicon: an MCU pad, or - for DIN and PDIO -
	   an expander address and bit, because those are not pads and saying
	   so is the point. */
	const char *pad;
};

struct pmc_block {
	float       x, y, w;
	bool        top;
	const char *name;
};

#include "board_table.inc"

/* ----------------------------------------------------------- the snapshot */

/*
 * Taken once per render, before the first row provider is called, so that
 * every number on the page comes from the same instant. The page says so in
 * its own heading, and this static is what makes that sentence true.
 */
static struct pmc_snapshot gSnap;

/* --------------------------------------------------------------- variables */

static char     vLink[64];
static char     vEth[72];
static char     vEthLedG[12];
static char     vEthLedA[12];
static char     vHost[48];
static char     vFw[24];
static uint32_t vUptime;

/* The clock marker. vClkTime is what the SERVER renders, one character per
   cell, so the page is correct with JavaScript disabled - the script only
   makes those cells tick. vClkEpoch is 0 whenever the time is not real, which
   is what the script tests before it starts ticking at all. */
static uint32_t vClkEpoch;
static char     vRail24[20];
static char     vRail12[20];
static char     vRailNote[72];
static char     vClkTime[12];
static char     vClkLabel[56];

static char rCCh[2];
static char rCCls[8];

static bool vPickOn;
static char vPickCh[20];
static char vPickKind[12];
static char vPickLabel[12];
static char vPickBlock[28];
static char vPickVal[72];
static char vPickConf[72];

/* The channel the browser asked about, or "" - compared by NAME against the
   terminal table, which is the allow-list. Nothing here ever takes an index,
   a port bit or an expander address from a request. */
static char gPick[20];

static char rHName[28];
static bool rHOk;
static char rHDetail[80];

static uint32_t rSSlot;
static char     rSVer[24];
static char     rSState[44];

static char    rAName[8];
static char    rAPad[8];
static int32_t rARaw;
static int32_t rAMv;
static bool    rAOpen;

static char  rBXform[40];
static float rBW;
static char  rBName[32];
static bool  rBTop;
static uint16_t rBTitleW;

static char rTXform[40];
static char rTClass[72];
static char rTHref[28];
static char rTLabel[12];
static char rTTip[176];
static bool rTTop;
static char rTBody[20];
static bool rTBox;
static char rTSilkCls[12];
static bool rTLed;
static char rTLedCls[20];

static void put(char *dst, size_t cap, const char *src)
{
	if (src == nullptr) {
		dst[0] = '\0';
		return;
	}
	strncpy(dst, src, cap - 1);
	dst[cap - 1] = '\0';
}

#define PUT(DST, SRC) put((DST), sizeof(DST), (SRC))

/*
 * The link as one line of prose, because the page wants to be readable and
 * /state.json already carries every field numerically for anything that wants
 * to compute. A sample that has never succeeded says so rather than rendering
 * a plausible 0 dBm.
 */
static void fillLink(void)
{
	if (gSnap.link.age_ms < 0) {
		PUT(vLink, "no link sample yet");
		return;
	}

	(void)snprintk(vLink, sizeof(vLink),
		       "%d dBm, %u.%u rate, ch %u%s",
		       (int)gSnap.link.rssi,
		       (unsigned)(gSnap.link.rate_tenths / 10U),
		       (unsigned)(gSnap.link.rate_tenths % 10U),
		       (unsigned)gSnap.link.chan,
		       (gSnap.link.age_ms > 10000) ? " (stale)" : "");
}

/*
 * The wire, in the same one line of prose. Each distinct state says which it
 * is, because "eth down" covers three different situations that want three
 * different actions: not built, built with no cable, and a cable whose PHY
 * will not come up.
 */
static void fillEth(void)
{
	/*
	 * THE JACK'S OWN TWO LAMPS. Green follows the PHY's link bit. Amber
	 * blinks whenever the link is up and is a CSS animation, NOT a traffic
	 * measurement - the board has no per-packet activity signal to offer,
	 * and the real amber blinks on any wire traffic the PHY sees, most of
	 * which never reaches this controller anyway.
	 */
	const bool up = (gSnap.eth.present && gSnap.eth.link_up);

	PUT(vEthLedG, up ? "ledg" : "ledg off");
	PUT(vEthLedA, up ? "leda act" : "leda off");

	if (!gSnap.eth.present) {
		PUT(vEth, "eth off in this build");
		return;
	}
	if (gSnap.eth.age_ms < 0) {
		PUT(vEth, "eth not sampled yet");
		return;
	}

	/*
	 * THE MAC CAN BE DEAD WHILE THE WIRE IS PERFECTLY FINE, which is this
	 * board's actual state. `carrier` comes from the MAC driver, so it
	 * reads false when that driver never initialized - and rendering "no
	 * cable" there would blame the cable for a controller fault. When the
	 * register probe ran, the PHY's own MDIO answer is the truth instead,
	 * and the text names the MAC as the part that is down.
	 */
	if (gSnap.eth.probed) {
		if (!up) {
			PUT(vEth, "eth PHY link down, MAC not initialized");
		} else if (gSnap.eth.speed_mbit == 0U) {
			/*
			 * Link up with nothing negotiated is a real state, not
			 * a missing reading: measured here as BMSR 0x780d -
			 * link bit set, auto-negotiation NOT complete - with
			 * ANLPAR 0x0001, a partner advertising no ability. "0
			 * half" would be a fabricated speed.
			 */
			PUT(vEth, "eth PHY link up, speed unnegotiated, MAC down");
		} else {
			(void)snprintk(vEth, sizeof(vEth),
				       "eth PHY link %u %s, MAC not initialized",
				       (unsigned)gSnap.eth.speed_mbit,
				       gSnap.eth.full_duplex ? "full" : "half");
		}
		return;
	}

	if (!gSnap.eth.carrier) {
		PUT(vEth, "eth no cable");
		return;
	}
	if (!gSnap.eth.link_up) {
		PUT(vEth, "eth cable in, PHY link down");
		return;
	}

	(void)snprintk(vEth, sizeof(vEth), "eth %u %s%s",
		       (unsigned)gSnap.eth.speed_mbit,
		       gSnap.eth.full_duplex ? "full" : "half",
		       gSnap.eth.has_addr ? "" : ", no address");
}

/*
 * THE CLOCK MARKER'S THREE STATES, which must not render alike.
 *
 * A clock that was held through a power-off, a part that says it lost time,
 * and no part at all are three different facts. vClkEpoch stays 0 for the last
 * two, which is what stops the browser-side tick from starting: a ticking
 * "--:--:--" would be the worst of the three renderings.
 *
 * UTC, deliberately. There is no timezone in the build and inventing one in
 * the renderer would make every timestamp on this page a claim about a setting
 * nobody chose. The label says UTC so the reader is not guessing.
 */
/*
 * THE RAIL LAMPS. 24 V is lit only when all six encoder pulls read high; if
 * anything is plugged into J10 the inference is void, the lamp goes dark and
 * the note says why. 12 V follows 24 V and nothing else - an LDO off the same
 * rail with no sense path of its own - so it is lit on the same evidence and
 * never on its own.
 */
static void fillRail(void)
{
	const bool up = gSnap.rail.sensed && gSnap.rail.present;

	PUT(vRail24, up ? "led green on" : "led green");
	PUT(vRail12, up ? "led amber on" : "led amber");

	if (!gSnap.rail.sensed) {
		PUT(vRailNote, "24V/12V not sensed - encoder pins unreadable");
	} else if (gSnap.rail.wired) {
		(void)snprintk(vRailNote, sizeof(vRailNote),
			       "24V unknown - %u of 6 encoder pins high, so "
			       "something is wired to J10",
			       (unsigned)gSnap.rail.highs);
	} else if (up) {
		PUT(vRailNote, "24V inferred present from the encoder pull-ups");
	} else {
		PUT(vRailNote, "24V inferred ABSENT - all encoder pulls are low");
	}
}

static void fillClock(void)
{
	vClkEpoch = 0;

	switch (gSnap.clock_state) {
	case PMC_CLOCK_HELD: {
		const int64_t e = gSnap.clock_epoch;
		const uint32_t tod = (uint32_t)(((e % 86400) + 86400) % 86400);

		vClkEpoch = (uint32_t)e;
		snprintk(vClkTime, sizeof(vClkTime), "%02u:%02u:%02u",
			 tod / 3600U, (tod / 60U) % 60U, tod % 60U);

		/*
		 * THREE DIFFERENT THINGS A WORKING CLOCK CAN MEAN, and the
		 * label is the only place they are distinguishable: a rate
		 * that has actually been measured, a clock NTP set but has
		 * not yet had time to measure, and one the supercapacitor
		 * carried with nothing having checked it against UTC at all.
		 */
		if (gSnap.drift.valid) {
			const int32_t t10 = gSnap.drift.ppm_tenths;
			const int32_t mag = (t10 < 0) ? -t10 : t10;

			snprintk(vClkLabel, sizeof(vClkLabel),
				 "UTC - %s%d.%d ppm over %u h",
				 (t10 < 0) ? "-" : "+", (int)(mag / 10),
				 (int)(mag % 10), gSnap.drift.span_s / 3600U);
		} else if (gSnap.drift.set_epoch != 0) {
			PUT(vClkLabel, "UTC - set from NTP");
		} else {
			PUT(vClkLabel, "UTC - held through power-off");
		}
		break;
	}
	case PMC_CLOCK_LOST:
		/*
		 * "Lost power" was the old wording and it claimed too much.
		 * The VL bit is set at power-on and cleared only by a write,
		 * so it means "the time in me is not trustworthy" - which
		 * covers a clock that was never set at all, the state every
		 * board is in until the first NTP query lands.
		 */
		PUT(vClkTime, "--:--:--");
		PUT(vClkLabel, "no time yet - waiting for NTP");
		break;
	default:
		PUT(vClkTime, "--:--:--");
		PUT(vClkLabel, "no RTC in this image");
		break;
	}
}

/* ---------------------------------------------------------- row providers */

static uint16_t healthCount(void)
{
	return (uint16_t)pmc_health_check_count();
}

static bool healthSelect(uint16_t i)
{
	const char *detail = "";

	if (i >= pmc_health_check_count()) {
		return false;
	}
	rHOk = pmc_health_check(i, &detail);
	PUT(rHName, pmc_health_check_name(i));
	PUT(rHDetail, detail);
	return true;
}

static uint16_t slotCount(void)
{
	return (uint16_t)gSnap.n_slot;
}

static bool slotSelect(uint16_t i)
{
	if (i >= gSnap.n_slot) {
		return false;
	}
	rSSlot = gSnap.slot[i].slot;
	PUT(rSVer, gSnap.slot[i].present ? gSnap.slot[i].version : "empty");
	(void)snprintf(rSState, sizeof(rSState), "%s, %s",
		       gSnap.slot[i].active ? "running" : "standby",
		       gSnap.slot[i].confirmed ? "confirmed" : "unconfirmed");
	return true;
}

/*
 * ONE CELL PER CHARACTER of the rendered time, including the colons.
 *
 * A flip-card readout needs each digit in its own box, and eight boxes cannot
 * be eight registry variables for the same reason seventy-nine terminals
 * cannot. The row provider walks the string the server already formatted, so
 * the markup does not care whether that string is a time or "--:--:--".
 */
static uint16_t clkCount(void)
{
	return (uint16_t)strlen(vClkTime);
}

static bool clkSelect(uint16_t i)
{
	if (i >= strlen(vClkTime)) {
		return false;
	}
	rCCh[0] = vClkTime[i];
	rCCh[1] = '\0';
	PUT(rCCls, (vClkTime[i] == ':') ? "ccw-c" : "ccw-d");
	return true;
}

static uint16_t aiCount(void)
{
	return (uint16_t)gSnap.n_ai;
}

static bool aiSelect(uint16_t i)
{
	if (i >= gSnap.n_ai) {
		return false;
	}
	PUT(rAName, gSnap.ai[i].name);
	PUT(rAPad, gSnap.ai[i].pad);
	rARaw = gSnap.ai[i].raw;
	rAMv = gSnap.ai[i].mv;
	rAOpen = gSnap.ai[i].open;
	return true;
}

static uint16_t blkCount(void)
{
	return (uint16_t)ARRAY_SIZE(pmc_blocks);
}

static bool blkSelect(uint16_t i)
{
	if (i >= ARRAY_SIZE(pmc_blocks)) {
		return false;
	}
	(void)snprintf(rBXform, sizeof(rBXform), "translate(%.1f %.1f)",
		       (double)pmc_blocks[i].x, (double)pmc_blocks[i].y);
	rBW = pmc_blocks[i].w;
	rBTop = pmc_blocks[i].top;
	/* 12px monospace with .11em tracking is about 8.5 units a character;
	   the box wants a little air either side of the title. */
	rBTitleW = (uint16_t)(strlen(pmc_blocks[i].name) * 85U / 10U + 12U);
	PUT(rBName, pmc_blocks[i].name);
	return true;
}

static uint16_t termCount(void)
{
	return (uint16_t)ARRAY_SIZE(pmc_terms);
}

static bool termSelect(uint16_t i)
{
	if (i >= ARRAY_SIZE(pmc_terms)) {
		return false;
	}

	const struct pmc_term &t = pmc_terms[i];
	const bool is_chan = (t.chan[0] != '\0');
	const bool measured = (strcmp(t.conf, "measured") == 0);
	char reading[72];
	const int level = pmc_snapshot_reading(&gSnap, t.chan, reading,
					       sizeof(reading));

	(void)snprintf(rTXform, sizeof(rTXform), "translate(%.1f %.1f)",
		       (double)t.x, (double)t.y);
	rTTop = t.top;
	PUT(rTBody, t.top ? "rotate(180 0 44)" : "translate(0 0)");
	/* Derived, not a table column: the box is a property of the
	   label and GND is the only label that carries one. */
	rTBox = (strcmp(t.label, "GND") == 0);
	PUT(rTSilkCls, rTBox ? "silk inv" : "silk");
	rTLed = t.led;
	PUT(rTLabel, t.label);

	/*
	 * The class string carries four independent facts, and the one that
	 * matters most is `unverified`. A terminal whose mapping came off a PDF
	 * and one confirmed with a meter at the connector MUST NOT render
	 * identically: the whole hazard of this panel is a plausible map that
	 * energizes the wrong 24 V output. CSS draws the unverified ones with a
	 * dashed screw block, and tools/terminals.csv says which are which.
	 */
	(void)snprintf(rTClass, sizeof(rTClass), "term k-%s%s%s%s%s", t.kind,
		       is_chan ? "" : " nochan",
		       (is_chan && !measured) ? " unverified" : "",
		       level == 1 ? " live-hi" : (level == 0 ? " live-lo" : ""),
		       (is_chan && strcmp(t.chan, gPick) == 0) ? " sel" : "");

	/* A channel LED that nothing drives stays dark. There is no "probably
	 * off": see snapshot.h on why an absent reader and a reading of 0 are
	 * not the same thing. */
	PUT(rTLedCls, level == 1 ? "chled on" : "chled");

	(void)snprintf(rTHref, sizeof(rTHref), "?ch=%s",
		       is_chan ? t.chan : "");

	if (is_chan) {
		/* THREE NAMES, because they are three different things and the
		   confusion between them cost a day: the terminal as it is
		   silkscreened, the software channel name, and what the channel
		   actually is in silicon. The last is an MCU pad for most
		   families and an expander bit for DIN and PDIO - never a pad
		   name invented to look like one. */
		(void)snprintf(rTTip, sizeof(rTTip),
			       "%s = %s [%s] %s on %s - %s - %s",
			       t.label, t.chan, t.pad, t.kind,
			       t.block, reading,
			       measured ? "confirmed at the connector"
					: "pinout mapping, NOT confirmed at "
					  "the connector");
	} else {
		(void)snprintf(rTTip, sizeof(rTTip), "%s on %s - not a channel",
			       t.label, t.block);
	}
	return true;
}

/* ----------------------------------------------------------- the registry */

TPL_REGISTRY_BEGIN(gPmcVars)
	/* System. All read-only: nothing on this page is a form field, and a
	   variable the firmware owns must say so in the registry rather than
	   relying on there being no POST route yet. */
	TPL_STR_RO("link", vLink)
	TPL_STR_RO("eth", vEth)
	TPL_STR_RO("ethLedG", vEthLedG)
	TPL_STR_RO("ethLedA", vEthLedA)
	TPL_STR_RO("host", vHost)
	TPL_STR_RO("fw", vFw)
	TPL_VAR_RO("uptime", vUptime)
	TPL_VAR_RO("clkEpoch", vClkEpoch)
	TPL_STR_RO("clkLabel", vClkLabel)
	TPL_STR_RO("rail24", vRail24)
	TPL_STR_RO("rail12", vRail12)
	TPL_STR_RO("railNote", vRailNote)

	/* The clicked terminal. */
	TPL_VAR_RO("pickOn", vPickOn)
	TPL_STR_RO("pickCh", vPickCh)
	TPL_STR_RO("pickKind", vPickKind)
	TPL_STR_RO("pickLabel", vPickLabel)
	TPL_STR_RO("pickBlock", vPickBlock)
	TPL_STR_RO("pickVal", vPickVal)
	TPL_STR_RO("pickConf", vPickConf)

	TPL_EACH("clk", clkCount, clkSelect)
	TPL_ROW_STR("cCh", rCCh)
	TPL_ROW_STR("cCls", rCCls)

	TPL_EACH("health", healthCount, healthSelect)
	TPL_ROW_STR("hName", rHName)
	TPL_ROW("hOk", rHOk, 0)
	TPL_ROW_STR("hDetail", rHDetail)

	TPL_EACH("slot", slotCount, slotSelect)
	TPL_ROW("sSlot", rSSlot, 0)
	TPL_ROW_STR("sVer", rSVer)
	TPL_ROW_STR("sState", rSState)

	TPL_EACH("ai", aiCount, aiSelect)
	TPL_ROW_STR("aName", rAName)
	TPL_ROW_STR("aPad", rAPad)
	TPL_ROW("aRaw", rARaw, 0)
	TPL_ROW("aMv", rAMv, 0)
	TPL_ROW("aOpen", rAOpen, 0)

	TPL_EACH("blk", blkCount, blkSelect)
	TPL_ROW_STR("bXform", rBXform)
	TPL_ROW("bW", rBW, 1)
	TPL_ROW_STR("bName", rBName)
	TPL_ROW("bTop", rBTop, 0)
	TPL_ROW("bTitleW", rBTitleW, 1)

	TPL_EACH("term", termCount, termSelect)
	TPL_ROW_STR("tXform", rTXform)
	TPL_ROW_STR("tClass", rTClass)
	TPL_ROW_STR("tHref", rTHref)
	TPL_ROW_STR("tLabel", rTLabel)
	TPL_ROW_STR("tTip", rTTip)
	TPL_ROW("tTop", rTTop, 0)
	TPL_ROW_STR("tBody", rTBody)
	TPL_ROW("tBox", rTBox, 0)
	TPL_ROW_STR("tSilkCls", rTSilkCls)
	TPL_ROW("tLed", rTLed, 0)
	TPL_ROW_STR("tLedCls", rTLedCls)
TPL_REGISTRY_END(gPmcVars)

static HtmlTpl gTpl(gPmcVars);
HTMLTPL_ZEPHYR_CTX_DEFINE(gPanel);

/* ------------------------------------------------------------- the picker */

/*
 * Pulls ch=<name> out of a query string, then looks that NAME up in the
 * terminal table.
 *
 * NOT PERCENT-DECODED, and that is a decision rather than an omission. Channel
 * names are letters, digits and underscores, so a legitimate request never
 * needs an escape; anything containing one therefore fails to match any row
 * and renders as nothing picked. Decoding first would mean writing a decoder
 * whose only job is to let a crafted value reach a comparison it is going to
 * fail anyway.
 */
static void pick(const char *q, size_t qlen)
{
	vPickOn = false;
	gPick[0] = '\0';
	vPickCh[0] = '\0';
	vPickKind[0] = '\0';
	vPickLabel[0] = '\0';
	vPickBlock[0] = '\0';
	vPickVal[0] = '\0';
	vPickConf[0] = '\0';

	if (q == nullptr || qlen == 0U) {
		return;
	}

	/* Find "ch=" at the start of a parameter, i.e. after ? or &. */
	size_t i = 0;
	while (i < qlen && q[i] != '?' && q[i] != '&') {
		i++;
	}
	const char *val = nullptr;
	size_t vlen = 0;

	while (i < qlen) {
		i++; /* step over the ? or & */
		if (qlen - i >= 3U && strncmp(q + i, "ch=", 3) == 0) {
			val = q + i + 3;
			vlen = 0;
			while (i + 3U + vlen < qlen && val[vlen] != '&') {
				vlen++;
			}
			break;
		}
		while (i < qlen && q[i] != '&') {
			i++;
		}
	}

	if (val == nullptr || vlen == 0U || vlen >= sizeof(gPick)) {
		return;
	}
	memcpy(gPick, val, vlen);
	gPick[vlen] = '\0';

	for (size_t k = 0; k < ARRAY_SIZE(pmc_terms); k++) {
		const struct pmc_term &t = pmc_terms[k];

		if (t.chan[0] == '\0' || strcmp(t.chan, gPick) != 0) {
			continue;
		}
		vPickOn = true;
		PUT(vPickCh, t.chan);
		PUT(vPickKind, t.kind);
		PUT(vPickLabel, t.label);
		PUT(vPickBlock, t.block);
		(void)pmc_snapshot_reading(&gSnap, t.chan, vPickVal,
					   sizeof(vPickVal));
		PUT(vPickConf, strcmp(t.conf, "measured") == 0
				       ? "confirmed at the connector"
				       : "pinout mapping, NOT confirmed at the "
					 "connector");
		return;
	}

	/* An unknown name is not an error worth a status code - a stale
	 * bookmark is the likely cause - but it is worth counting, because a
	 * stream of them is somebody probing the one request parameter this
	 * panel accepts. */
	LOG_INF("unknown channel in query, ignored (%u bytes)",
		(unsigned)vlen);
	gPick[0] = '\0';
}

/* --------------------------------------------------------- entry points */

/*
 * A Print that forwards whole lines to the Zephyr log.
 *
 * Needed because selfTest() reports faults into a Print& rather than returning
 * them, and a sink that swallowed them would turn the self-test into a check
 * that reports a COUNT and not a cause - which is most of the way to not
 * having run it. The engine writes each fault as one <!--#!ERR ...--> run, so
 * flushing on the closing newline, on a full buffer, or at destruction gets
 * each one onto its own log line.
 */
class PmcLogPrint : public Print {
public:
	~PmcLogPrint() override { flush(); }

	size_t write(uint8_t c) override
	{
		if (c == '\n' || used_ == sizeof(buf_) - 1U) {
			flush();
			if (c == '\n') {
				return 1;
			}
		}
		buf_[used_++] = (char)c;
		return 1;
	}

	void flush()
	{
		if (used_ == 0U) {
			return;
		}
		buf_[used_] = '\0';
		used_ = 0;
		LOG_ERR("page self-test: %s", buf_);
	}

private:
	char   buf_[128];
	size_t used_ = 0;
};

extern "C" void pmc_page_init(void)
{
	HTMLTPL_ZEPHYR_INIT(gPanel);

	/*
	 * Render the page once, to nowhere, and report every tag that does not
	 * resolve against this registry.
	 *
	 * The page is compiled into this image, so it cannot have been edited
	 * behind the firmware's back - mkhtml.py already refused the build if a
	 * name was wrong. What this catches is the other direction: a registry
	 * edited after the header was generated, which is exactly the stale-
	 * generated-header case this repository keeps running into. It costs
	 * about a millisecond and it calls every row provider once, which also
	 * proves they do not fault before a browser is pointed at them.
	 */
	PmcLogPrint sink;

	pmc_snapshot_take(&gSnap);
	PUT(vHost, gSnap.host);
	PUT(vFw, gSnap.fw);
	vUptime = gSnap.uptime_s;
	fillClock();
	fillRail();
	fillLink();
	fillEth();

	const uint16_t faults = gTpl.selfTest(TPL_FROM_FLASH(PAGE_INDEX), sink);

	if (faults != 0U) {
		LOG_ERR("page self-test: %u fault(s) - the panel will render "
			"holes where those tags are", faults);
	} else {
		LOG_INF("page self-test clean (%u bytes, build %s)",
			(unsigned)PAGE_INDEX_BYTES, PAGE_INDEX_BUILD);
	}
}

extern "C" int pmc_page_step(enum http_transaction_status status,
			     struct http_response_ctx *rsp, const char *query,
			     size_t qlen)
{
	static const struct http_header hdrs[] = {
		{ .name = "Content-Type", .value = "text/html; charset=utf-8" },
		/*
		 * NO-STORE, because the body does not exist until the request
		 * arrives. A rendered template has no ETag worth sending: it
		 * would have to be computed by rendering, which is the work the
		 * ETag exists to avoid.
		 */
		{ .name = "Cache-Control", .value = "no-store" },
	};

	if (status == HTTP_SERVER_REQUEST_DATA_FINAL && !gPanel.live) {
		/*
		 * The first callback of this response, and the ONLY one that is
		 * handed the query string - Zephyr passes it once and then
		 * zeroes the length. It is also where the snapshot belongs, so
		 * that every row the render is about to ask for comes from one
		 * pass over the hardware.
		 */
		pmc_snapshot_take(&gSnap);
		PUT(vHost, gSnap.host);
		PUT(vFw, gSnap.fw);
		vUptime = gSnap.uptime_s;
		fillClock();
		fillRail();
		fillLink();
		fillEth();
		pick(query, qlen);
	}

	return htmlTplServeZephyr(gPanel, status, rsp, gTpl,
				  TPL_FROM_FLASH(PAGE_INDEX), hdrs,
				  ARRAY_SIZE(hdrs));
}

extern "C" size_t pmc_page_state_json(char *out, size_t cap)
{
	pmc_snapshot_take(&gSnap);
	return pmc_snapshot_json(&gSnap, out, cap);
}
