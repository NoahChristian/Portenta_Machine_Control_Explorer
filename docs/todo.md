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
- **Analog output range and resolution.** Nothing measured at all yet, and
  the output is a filtered PWM into a gain-of-3.3 amplifier rather than a DAC,
  so resolution and settling are properties of the filter. AO2 is further
  limited to a ~1.3 ms period by HRTIM (see below).

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

**Answered 2026-11-16, and the answer is no.** From the two datasheets and
Zephyr's own pinctrl files.

An earlier version of this entry said the X8 has no STM32H7. That was wrong.
It has one — `STM32H747AII6` (U20) — but in the **AI package, where this
carrier is wired for the XI**, and that is the whole story. Zephyr's pinctrl
files describe 112 pins for the AI package against 168 for the XI, and six of
the pins this carrier names are absent from the smaller part outright:

| pin | what it does here |
|---|---|
| `PJ_11` | AO0 |
| `PK_1` | AO1 |
| `PI_0` | thermocouple chip select, `MC_TC_CS` |
| `PI_4` | temperature mux `SEL1` |
| `PH_14` | PDIO latch |
| `PH_13` | CAN |

Ports J and K do not exist on the AI package at all. So two of four analog
outputs, half the temperature front end, the PDIO latch and CAN are gone
before any software question is asked. Checked by grepping
`stm32h747a(g-i)ix-pinctrl.dtsi` against `stm32h747xihx-pinctrl.dtsi`.

**The software side is a design change, not a port** — and not the flat "no"
an earlier draft of this entry claimed. The X8 datasheet, section 5-2, says
its H7 is "used as an I/O expander for the NXP i.MX 8M Mini": the M7 runs
Arduino's own firmware (`arduino/portentax8-stm32h7-fw`) mapping peripherals
as Linux devices, and sketches are expected on the M4. But that firmware is
**open and replaceable**, and the same section says so outright — "advanced
users can customize the M7 firmware to fit their needs but this could break
the seamless Linux integration". So the M7 is available to anyone willing to
give up the Linux device mapping, and the earlier wording here, that there was
"no seat" for a native Zephyr application, was wrong.

What is genuinely unavailable is **the network**. On the X8 the Murata radio
(U9) "interfaces with i.MX 8M Mini (U2) via a 4bit SDIO and UART interface",
and the 10/100/1000 Ethernet controller belongs to the i.MX as well — which is
what the datasheet means by the STM32H7 being "not exposed to networking". The
H7 has no interface of its own to serve from. This project is a Zephyr
application that owns the M7 and answers HTTP on its own IP address; on an X8
that panel has to be tunnelled through Linux, which is a different program
with a different failure surface. Combined with six pins that do not exist,
the honest summary is: portable in spirit, not by recompiling.

#### Is the X8's Linux side open source, or a binary drop?

A split, which is why it is worth writing down rather than answering yes or
no: **the operating system is source; the silicon-enablement layer under it is
binary.**

Source, on GitHub, GPL-2.0: `nxp-imx/linux-imx` (kernel, GPL-2.0 WITH
Linux-syscall-note), `nxp-imx/uboot-imx`, `nxp-imx/meta-imx` (the Yocto
layers, carrying a per-release Software Content Register that names every
package's license), and `imx-atf`. Arduino's layer on top is source too —
`arduino/lmp-manifest` and `arduino/meta-partner-arduino`, with a documented
bitbake build.

Binary only, under `LA_OPT_NXP_Software_License`:

- `firmware-imx-*.bin` — DDR PHY training (`lpddr4_pmu_train_*`), plus VPU and
  HDMI firmware. The `nxp-imx/imx-firmware` repository contains **only a
  LICENSE.txt**: the repo is the license, not the code.
- `imx-gpu-viv` — the Vivante GPU userspace. Its Yocto recipe states
  `LICENSE = "Proprietary"` and ships a self-extracting `.bin`.

The mechanism is visible in the build itself: NXP's Yocto guide requires
accepting the license during setup so the proprietary packages can be
extracted at build time. You bitbake real sources, and partway through, the
build fetches binaries you agreed to.

**Why a RAM controller needs firmware at all**, since it reads like something
that ought to be a register write. LPDDR4 runs at gigabits per second per pin,
where usable margin is tens of picoseconds and depends on trace length, the
DRAM die, temperature and voltage. The controller therefore cannot be
*configured*, it has to be *measured* at every boot: write leveling, read gate
training, per-bit deskew, then eye centering across both voltage and timing —
which is the `1d` and `2d` in the filenames. That measurement loop does not
run on the A-cores. The DDR PHY is a licensed Synopsys macro with **its own
small embedded processor inside it**, and the blobs are that processor's
instruction and data memory images, which is exactly why they are named
`..._imem.bin` and `..._dmem.bin`. NXP ships them compiled because NXP does
not own the source.

Structurally that is the same arrangement as the 421 KB `4343WA1.bin` already
compiled into this project's image: firmware for somebody else's embedded
core. The difference is consequence. The WiFi blob costs you WiFi; the DDR
blob is required to bring up RAM at all, so there is no degraded mode without
it.

Purism's workaround, which comes up whenever this is discussed, does **not**
liberate the code. They kept the blobs and changed which processor handles
them: the i.MX's own Cortex-M4 loads them into the PHY, and U-Boot on the A53
then runs the training, so U-Boot and the kernel stay blob-free. The FSF
accepted that under the "secondary processor" exception to Respects Your
Freedom, with the blobs intended to live in read-only SPI flash so they cannot
be changed without the user knowing. A certification boundary was moved, not a
reimplementation written.

*Waiting on:* nothing. Reopen only if Arduino ships an X8 in the XI package,
which would change the first half and none of the second. Whether the
Machine Control's own H7 can be removed at all is still unstated: its
datasheet never presents the H7 as a module, mentions no high-density
connector anywhere, and lists `STM32H747XI` as a part of the product.

### RTC drift against NTP

**Use the carrier's clock.** Datasheet section 3-20: a PCF8563T/F4 on a
dedicated 32.768 kHz crystal, with a 100 mF supercapacitor holding it up for
at least 48 hours once board power is removed. The SoC's internal RTC has no
backup here.

**Nothing is missing from the toolchain**, which was worth checking rather
than assuming. Zephyr carries `drivers/rtc/rtc_pcf8563.c` with the
`nxp,pcf8563` binding, the generic `rtc.h` API, and SNTP in
`subsys/net/lib/sntp`. This application's `prj.conf` names none of them. It is
a devicetree node and a Kconfig line, not a driver that has to be written.

**The feature, which is smaller than it first looks:**

1. **At boot, read the RTC.** The part answers the question itself: the VL bit
   in its seconds register means the oscillator stopped, and the driver turns
   that into `-ENODATA` from `rtc_get_time()`. A clean read means the
   supercapacitor held and the board already knows the time with no network
   involved — switch the bench on and the timestamps are real immediately.
2. **If the clock is unset and NTP answers, set it**, and keep the moment it
   was set.
3. **Later — hours, not minutes — ask NTP again** and read the RTC beside it.
   The error is the difference between them, and
   `ppm = error_seconds / interval_seconds * 1e6`.
4. **Report the last error in ppm**, with the interval it was measured over,
   next to the time and where the time came from: held through a power-off,
   or set from NTP at a stated moment.

Nothing here counts edges and nothing needs a second time base. The thing
being measured is an independent part read over I2C and the reference is NTP,
so no clock is being asked to measure itself. (That failure does exist, but in
a different feature: disciplining the SYSTEM clock from NTP and then trying to
measure its rate between syncs cannot work, because the sync steps the clock
under measurement.)

Two things for whoever writes it:

- Zephyr's PCF8563 driver never touches the CLKOUT control register, so
  anything wanting a 1 Hz edge to count would need direct register writes. The
  design above does not want one.
- That driver's comment says "if the first bit is 1 it is ok" while the code
  returns `-ENODATA` when bit 7 is set. The code is right and matches the
  part: VL set means integrity is **not** guaranteed.

Separate and lower priority: the build target carries no board revision, so it
takes the 1.0.0 default, which clocks the SoC's internal RTC from LSI rather
than 4.10.0's LSE. Irrelevant while the wall clock is the carrier's part;
wrong the moment anything uses the internal one.

*Waiting on:* a `pcf8563@51` node on the Portenta's internal bus, `CONFIG_RTC`
and SNTP in the build. One commit's work, and nothing in it is blocked on
hardware.
and SNTP in the build. All of it is one commit's work; none of it is blocked
on hardware that is missing.

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

- **AO2 on `PG_7` is correct after all, and Zephyr cannot express it.**
  Resolved 2026-11-16 from the datasheet: AO2 is driven by **HRTIM**, not a
  general-purpose timer, which is exactly why that pin carries no `tim*_ch*`
  function — see HARDWARE_FROM_MANUAL.md, section 3-7. Zephyr has no HRTIM
  driver and no binding, and `PG7` appears in `stm32h747xihx-pinctrl.dtsi`
  with only `analog`, `dcmi_d13`, `fmc_int`, `ltdc_clk` and `sai1_mclk_a`, so
  the alternate function cannot even be named in a devicetree. AO0, AO1 and
  AO3 are ordinary `pwm`; AO2 needs hand-written LL or HAL code, or the panel
  renders it unavailable and says why. A Zephyr HRTIM driver would be a real
  upstream contribution and is the larger version of this task.
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
