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
