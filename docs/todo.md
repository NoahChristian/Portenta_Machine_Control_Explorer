# Punchlist

Unordered. Each item says what it is waiting on, because "not done yet" and
"blocked on a measurement" are different states and only one of them is work
somebody can pick up.

---

## Hardware questions this project exists to answer

These are the reasons the project is called Explorer. Each is something the
datasheet does not say and a user would have to discover.

### Modbus, as a working example rather than a claim

The RS485 transceiver has eight control GPIOs and the datasheet says nothing
about how to drive a protocol over it. There is visible consternation upstream
— `arduino-libraries/Arduino_PortentaMachineControl` issue 35 — and an MVP that
actually sends and receives on a port would be worth more than any amount of
description.

Scope for a first pass: one RTU master or slave, a handful of registers, and
the DE/RE timing made visible in the panel so the turnaround can be seen rather
than inferred. The eight control lines (DE, RE, EN, RS232 mode, full-duplex
term, term, slew, HF) are the interesting part — which combination is correct
for 2-wire RTU is not written down anywhere found so far.

*Waiting on:* the channel table, and a second RS485 device to talk to.

### USB on J15 — host or sink, and what for

The topology table lists `J15 USB A` and `J16 USB micro`, and
`pins_mc.h` has `MC_USB_PWR_PIN PB_14` and `MC_USB_FLAG_PIN PB_15` — a power
switch and its fault flag, which implies J15 is a **host** port supplying
power. But the datasheet does not say so, does not give the current limit, and
does not say what the intended use is. Nor is it clear how it interacts with
J16, which is presumably the device port the shell appears on.

*Waiting on:* reading the power-switch part number off the board, and trying a
device on J15 with the fault flag watched.

### Analog in/out bitness and range

Poorly documented, and the transfer functions below go most of the way. What
is still unestablished:

- **ADC resolution actually in use.** The SoC supports 16/14/12/10/8
  (`st,adc-resolutions`), the shield currently asks for 16, and the mbed
  library calls `analogReadResolution()` with whatever the caller passed and
  **no range check**. So "what do the raw counts mean" depends on a setting no
  document states.
- **Analog output range and resolution.** Nothing measured at all yet, and the
  pin mapping for AO2 is known to be wrong (see below).

*Waiting on:* a bench supply and a meter at the terminals.

### Source and sink absolute maximums

The headline numbers — 24 V, 500 mA per output — are in the datasheet. The
**absolute maximums**, which are what matters when sizing a load or deciding
whether a surge is survivable, are buried in documents beyond it: the
TPS4H160's own datasheet for the high-side switches, and whatever governs the
24 V input rail.

This is exactly the kind of fact an explorer should surface, because the
consequence of guessing is hardware damage rather than a wrong reading.

*Waiting on:* collecting the component datasheets and recording the numbers
with their source, in HARDWARE_FROM_MANUAL.md.

### Would a Portenta X8 drop into this carrier?

The X8 appears to share the High-Density footprint, which would make the
Machine Control a far more interesting board — Linux on the carrier, with the
same I/O. Open questions:

- Are both HD connectors pin-compatible for the signals this carrier uses?
- The reset arrangement differs, and the carrier has exactly one button wired
  to the module's reset.
- The X8 has no STM32H7, so every pin assignment in
  INFERRED_FROM_MBED.md is void — the expanders and the SPI temperature bus
  would be reachable, but the 45 direct GPIO assignments would not.

*Waiting on:* an X8, and a careful read of both HD pinouts side by side.

### RTC drift against NTP

Worth doing the way a sibling project on this bench does it: discipline or at least
*measure* the RTC against NTP, and report the drift. A PCF8563 at 0x51 has an
ordinary 32.768 kHz crystal and will drift; knowing by how much turns a
timestamp from a claim into a measurement.

Note the board revision matters here: `board.yml` declares revisions 1.0.0 and
4.10.0 with different RTC clock sources (LSI against LSE), so which crystal is
actually fitted changes the answer.

*Waiting on:* the RTC driver being enabled at all, and SNTP in the build.

---

## The panel, now that it renders

- **Nothing on the board drawing is `measured`.** All 56 channels in
  `tools/terminals.csv` say `pinout` or `photo`, so every one renders with a
  dashed screw block. Confirming a mapping at the connector with a meter and
  flipping that column is the single highest-value piece of bench work here,
  and it is what the dashes exist to keep anyone from forgetting.
- **The 28 channel LEDs never light**, because DIN, DO and PDIO live on the
  two TCA6424A expanders and nothing in the image reads them (M2). The
  snapshot reports "no reader in this image yet" rather than LOW, deliberately:
  an absent reader and a reading of zero must not render the same.
- **No write path yet.** `HTMLTPL_FORMS=0`, so the form binder and the CSRF
  token are not compiled in at all. M4 flips it to 1, adds
  `HtmlTplEntropyZephyr.cpp` upstream to seed the token from
  `sys_csrand_get()`, and adds `htmlTplFeedFromZephyr()` — which has to
  accumulate a POST body across several callbacks into a buffer that outlives
  the render, because the binder NUL-terminates decoded values in place.
- **The rendered page is 72 KB on the wire against a 12 KB template**, and
  about a sixth of that is the design-time sample attributes that
  `data-tpl-attr` leaves in place so the file still previews in a browser.
  That is the right trade today — nothing polls, so a page load is a rare
  event — but it is the first thing to look at if the panel ever feels slow.

## Known-wrong or unverified, carried from the code

- **AO2 on `PG_7` cannot be right.** That pin has no timer channel on this
  package and is not a DAC pin. Either the mapping is wrong or AO2 is driven
  some other way. Blocks any analog-output work.
- **Which expander is which.** 0x22 and 0x23 are measured; that ADDR-low is
  the programmable channels and ADDR-high the digital inputs is inferred from
  naming in the mbed library and nothing else.
- **The three scrambled channel maps** need confirming one channel at a time
  at the connector, because a transcription error produces a panel that
  confidently energizes the wrong 24 V output.
- **0x36 on the Portenta's internal I2C bus** is unidentified. It is on the
  module's bus rather than the carrier's, so it is more likely part of the H7.
- **The DO1 pin mapping** behind the boot indicator is inferred, not measured.
  `d1 walk` exists to settle it by watching which LED moves.

---

## Lessons to fold into the tooling

- **A fatal error must reset, not halt.** Zephyr's default fatal handler calls
  `arch_system_halt()`, which spins with interrupts off. On this board that
  kills the USB console and the network at once, so the only remaining recovery
  is a hand on RST — and it defeats the dual-slot design outright, because a
  RESET is exactly what makes MCUboot revert an image that has not confirmed.
  `CONFIG_RESET_ON_FATAL_ERROR=y` is set now. It was learned by halting the
  board with a stack overflow and having to ask for a physical reset.
- **`render()` puts its output buffer on the caller's stack**, 1460 bytes of
  it, so nothing on the system workqueue may render a template. The page
  self-test ran there once and overflowed a 2048-byte stack. Both the page
  engine and this repository say so now; the general form is that "internal
  buffer" in a library comment is worth reading as "on your stack" until
  proven otherwise.
- **A build that cannot be installed should fail, not warn.** `build.ps1`
  passed the application's signing key only with `-Mcuboot`, which is the flag
  you are told to omit, so the ordinary build produced an unsigned image and
  the NEXT command blamed the bootloader. The misattribution was the expensive
  part, not the missing flag.

- **A health check must be satisfiable by the image that contains it.** Adding
  the "panel answered a request" condition in the same image whose server was
  never started made that image permanently unconfirmable — and an unconfirmed
  image has a revert armed, which makes slot1 un-erasable, which blocks the
  next update. A gate that can deadlock the update path is worse than a gate
  that is slightly too lax. Consider a bounded grace period after which the
  gate confirms anyway and says it did so under protest.
- **Identify the board by asking it, not by port number.** There are two Zephyr
  boards on this bench with CDC-ACM shells, and the tools picked between them
  by position. They now probe for the right one, but `tools/wifi_provision.ps1`
  still excludes a port by hardcoded name.
- **Route every serial interaction through one redacting helper.** The
  redaction in `wifi_provision.ps1` works and was then bypassed by hand, which
  put a passphrase in a transcript. A helper that cannot be bypassed is the
  only kind that holds.
