# Measured on this board, on this bench

What this hardware and this build actually did. Every entry carries the date,
the command that produced it, and captured output pasted verbatim.

**Output is never retyped into a table.** A table is something a person
composed, and the whole value of captured output is that nobody did. If a
figure here looks tidier than the tool that produced it, distrust it.

The one edit made to captured output is that absolute paths under this
machine's account are replaced with `<sdk>` and `<workspace>`. Committed files
are read on another machine, where that prefix is useless, and the account name
is not what any of these captures are evidence of. Elisions are always marked.

This page is one of three, split by kind of confidence, and a fact has exactly
one home. Moving something up the ladder means **deleting** it from the page
below, not copying it:

| Page | Status of what is on it |
|---|---|
| [HARDWARE_FROM_MANUAL.md](HARDWARE_FROM_MANUAL.md) | read from a datasheet or manual; never seen on hardware |
| [INFERRED_FROM_MBED.md](INFERRED_FROM_MBED.md) | what another program believes; mbed-only, not authoritative about silicon |
| MEASURED.md (this page) | observed here |

---

## 2026-11-15 — Image size with WiFi compiled in

The number M0 exists to produce, because it decides the flash map and the map
cannot be changed cheaply once signed images exist.

### First, the failure that proved the stock map cannot work

Built against the board's own partition table, which gives slot0 384 KB:

```
west build -p always -b arduino_portenta_h7/stm32h747xx/m7 app -d build/app
```

```
<sdk>/gnu/arm-zephyr-eabi/.../bin/ld.bfd.exe: zephyr\zephyr_pre0.elf section `rodata' will not fit in region `FLASH'
<sdk>/gnu/arm-zephyr-eabi/.../bin/ld.bfd.exe: region `FLASH' overflowed by 422828 bytes
```

The overflow, 422,828 bytes, is within 1,407 bytes of **421,421** — the size of

```
modules/hal/infineon/zephyr/blobs/img/whd/resources/firmware/COMPONENT_4343W/4343WA1.bin
```

That blob is linked **into** the image. There is no Kconfig to load it from the
QSPI `airoc_firmware` partition that the board's own table reserves for exactly
that file. So the WiFi firmware alone is what overruns the stock slot.

### Then the real number, against the map in `pmc-partitions.dtsi`

This was first measured against a 1280 KB slot0, when the plan was still to
replace the Arduino bootloader at 0x08000000. That plan did not survive contact
with the hardware - see "BOOT0 is not reachable" below - so slot0 is now 1536 KB
and the percentage is lower. The byte count is the same measurement.

```
west build -p always -b arduino_portenta_h7/stm32h747xx/m7 app -d build/app
```

```
Memory region         Used Size  Region Size  %age Used
           FLASH:      816076 B      1280 KB     62.26%
             RAM:      163876 B       512 KB     31.26%
            ITCM:           0 B        64 KB      0.00%
            DTCM:           0 B       128 KB      0.00%
          EXTMEM:           0 B        16 MB      0.00%
           SRAM0:           0 B       512 KB      0.00%
           SRAM1:           0 B       128 KB      0.00%
           SRAM2:           0 B       128 KB      0.00%
           SRAM3:         16 KB        32 KB     50.00%
           SRAM4:           0 B        64 KB      0.00%
          SDRAM1:           0 B         8 MB      0.00%
```

```
$ ls -la build/app/zephyr/zephyr.bin
816076 build/app/zephyr/zephyr.bin
```

**816,076 bytes (797 KB).**

With the carrier shield, MCUboot and the `mcuboot`/`swapclear` shell commands
added, and measured against the final 1536 KB slot0:

```
Memory region         Used Size  Region Size  %age Used
           FLASH:        820 KB      1536 KB     53.39%
             RAM:      166052 B       512 KB     31.67%
```

```
zephyr.bin               839,680 bytes   53.39% of slot0
zephyr.signed.bin        839,831 bytes    53.4% of slot0
```

What this build contains: WiFi (AIROC CYW4343W), Ethernet, the IPv4/TCP/UDP
stack, DHCPv4, the HTTP server, mcumgr/SMP over UDP with `img_mgmt`, LittleFS,
NVS settings, the shell on USB CDC-ACM, and deferred logging. It contains **no
carrier I/O drivers and no MCUboot**, so the figure will grow.

### What the number settled

| Option | Usable image | Verdict |
|---|---|---|
| Board's own map, 384 KB slots | 384 KB | **impossible** — fails to link, above |
| Two internal slots, swap-using-move: boot 128 + slot0 896 + slot1 896 = 1920 KB | 896 − 128 = **768 KB** | **impossible** — swap-using-move reserves one sector of the primary, and 768 KB is 29 KB *less* than the image already needs |
| boot 256 + slot0 1280 internal, slot1 + scratch in QSPI | 1280 KB | **chosen**, 37.7% spare |

The middle option is the attractive one on paper — homogeneous 128 KB geometry,
no scratch area, and none of the asymmetric-slot traps. It was rejected on
arithmetic, and the arithmetic is only 29 KB wide, which is why it is recorded
here rather than merely dropped. See [FLASH.md](FLASH.md).

### BOOT0 is not reachable on this assembly

Established 2026-11-15, from the datasheets and confirmed at the bench.

The Giga port installs MCUboot at 0x08000000 over the STM32 ROM bootloader,
which is entered by holding BOOT0 at reset. That cannot be done here.

- The Machine Control's own datasheet topology table (section 3.2) lists
  exactly one button: `PB1 RESET Push Button`. A search of the whole datasheet
  text for `swd`, `jtag`, `debug`, `boot0` and `boot sel` returns nothing but
  unrelated matches on "resettable fuse".
- The Portenta H7's schematic (SL-ABX00042 V4.10) contains exactly one push
  button, designated PB1 - its pin references are `PIPB105` and `PIPB106`, and
  no other `PB*` or `SW*` button designator appears. The `SW1`/`SW2`/`SW3` nets
  in that schematic are the PF1550 PMIC's switching regulator nodes, not
  switches.
- BOOT0 exists on the H7 as a net and is brought out to the high-density
  connector as `FORCE_BOOTLOADER`. The carrier occupies that connector.
- Opening the enclosure found an unmarked button on the H7 module itself. Per
  the schematic that is PB1, the H7's own reset - not a boot select.

So there is no BOOT0 button, no SWD header and no JTAG header on this
assembly, and reaching BOOT0 would mean unmounting the module from the
carrier.

**What is used instead.** The Arduino DFU bootloader stays at 0x08000000 and
chainloads MCUboot from 0x08040000, which is the address that bootloader
already jumps to - the Zephyr board file says so in a comment, and MCUboot
links wherever `boot_partition` is because its own `app.overlay:3` sets
`zephyr,code-partition = &boot_partition`. Everything installs by double-tap
RESET over USB.

Verified by reading the built images rather than by reasoning:

```
MCUboot      initial SP 0x24006d00   reset PC 0x08041e1d   (inside boot_partition)
application  vector table at header offset 0x400: SP 0x2400f880  PC 0x080918a5
```

so the Arduino bootloader's jump to 0x08040000 lands in MCUboot's code, and
MCUboot's jump to slot0 + 0x400 lands in the application's.

This is strictly safer than the Giga arrangement: sector 0 is never written, so
the Arduino bootloader cannot be damaged by anything done here and double-tap
RESET always reaches it again.

## 2026-11-15 � The I2C expanders are on i2c3, not i2c1

The addresses inferred from the mbed library were right. The bus was a guess,
and it was wrong.

```
i2c scan i2c@40005400          (i2c1, PB6/PB7 - "I2C1 in datasheet")
00:             -- -- -- -- 08 -- -- -- -- -- -- --
30: -- -- -- -- -- -- 36 -- -- -- -- -- -- -- -- --
50: -- 51 -- -- -- -- -- -- -- -- -- -- -- -- -- --
3 devices found
```

```
i2c scan i2c@40005c00          (i2c3, PH7/PH8 - "I2C3 in datasheet")
20: -- -- 22 23 -- -- -- -- -- -- -- -- -- -- -- --
2 devices found
```

```
i2c scan i2c@58001c00          (i2c4, PH11/PH12 - "I2C2 in datasheet")
0 devices found
```

So:

| bus | devices | what |
|---|---|---|
| i2c1 | 0x08, 0x36, 0x51 | the Portenta's internal bus |
| **i2c3** | **0x22, 0x23** | **the carrier's two TCA6424A expanders** |
| i2c4 | none | nothing on it; disabled again |

0x08 is the PF1550 PMIC, which the board's own devicetree declares at
`pmic@8`. 0x51 is almost certainly the PCF8563 RTC - that is its standard
address and the carrier has an RTC with its interrupt on `MC_RTC_INT_PIN PB_9`.
**0x36 is unidentified** and is an open question; it is on the Portenta module's
internal bus, not the carrier's, so it is more likely part of the H7 than part
of the Machine Control.

Before the move, both expander nodes reported `DISABLED` in `device list` -
which is what a driver whose init ran and got no answer looks like. That is the
useful negative signal, and it is worth knowing that it is NOT the same as a
node that was never instantiated: a disabled-status node would not appear in
the list at all.

## 2026-11-15 � The SYSCFG analog switches, measured both ways

The trap, quantified. `pmca syscfg close` returns the switches to their silicon
reset state; `pmca syscfg open` is what the shield does at PRE_KERNEL_1.

Switches OPEN, which is the shield's default:

```
SYSCFG_PMCR analog-switch bits: 0x0e000000 of 0x0e000000

chan  pad     switch        raw        mV  twin pad if closed
AI0   PC_3C   open          767        38  PC3 / spi2_mosi (temperature bus)
AI1   PC_2C   open          522        26  PC2 / spi2_miso (temperature bus)
AI2   PA_1C   open          750        37  PA1 / eth_ref_clk (50 MHz RMII)
```

Switches CLOSED, which is what the silicon does at reset and what every
default Zephyr build on this board therefore has:

```
SYSCFG_PMCR analog-switch bits: 0x00000000 of 0x0e000000

chan  pad     switch        raw        mV  twin pad if closed
AI0   PC_3C   CLOSED        396        19  PC3 / spi2_mosi (temperature bus)
AI1   PC_2C   CLOSED        435        21  PC2 / spi2_miso (temperature bus)
AI2   PA_1C   CLOSED      31631      1592  PA1 / eth_ref_clk (50 MHz RMII)
```

**AI2 goes from 37 mV to 1592 mV, a factor of 43.** 1592 mV is almost exactly
half of 3.3 V, which is what an ADC reports when it samples a square wave much
faster than its sample-and-hold: the 50 MHz RMII reference clock. With the
switch closed, AI2 reads about 1.6 V regardless of what is connected to the
terminal block.

**AI0 and AI1 move much less** - 767 to 396 and 522 to 435 - and that is the
more dangerous half of the finding. SPI2 is idle while nothing is reading a
temperature, so the error is small enough to pass casual inspection. It would
change whenever the thermocouple or RTD front end is actually used, which makes
it an intermittent fault correlated with an unrelated subsystem.

Both directions were run because an experiment that can only be run in the good
direction proves nothing. Note the readings are raw pad voltages: the 0-10 V /
4-20 mA / NTC mode is a 4-bit GPIO switch network that nothing drives yet, so
the front end is in an undefined mode and none of these numbers mean anything
about a signal on the connector. What they establish is the coupling, not a
measurement.

### Toolchain this was measured with

```
Zephyr version: 4.5.99 (<workspace>/zephyr)
west 1.5.0, Zephyr SDK 1.0.1, arm-zephyr-eabi gcc 14.3.0
cmake 4.4.5, ninja 1.13.2, dtc 1.6.1
Board: arduino_portenta_h7, Revision: 1.0.0, qualifiers: stm32h747xx/m7
```

The board revision matters and is recorded for that reason: `board.yml` declares
revisions 1.0.0 and 4.10.0, which differ in RTC clock source (LSI against LSE).
1.0.0 is the default and is what these builds used. Nothing here has confirmed
which revision the physical board on this bench actually is.

---

## Still to measure

Listed so that the gap is visible rather than merely absent. Each of these is a
decision currently resting on something other than observation.

- **The three analog inputs, with the SYSCFG analog switches closed and then
  open.** `SYSCFG_PMCR` resets to 0, which means those switches are *closed*,
  tying `PA_1C`/`PC_2C`/`PC_3C` to `PA1`/`PC2`/`PC3` — which on this board are
  the Ethernet RMII clock and SPI2. A tree-wide search found no caller of
  `LL_SYSCFG_OpenAnalogSwitch` for the H7 anywhere in Zephyr or its modules.
  Until this is measured, every analog reading is suspect.
- **The two TCA6424A I2C addresses**, by `i2c scan`.
- **All 20 expander channel mappings**, one channel at a time, confirmed at the
  connector with a meter. The maps inferred from the mbed library are scrambled
  three different ways; see [INFERRED_FROM_MBED.md](INFERRED_FROM_MBED.md).
- **Which pin actually drives AO2.** `PG_7` has no timer channel on this
  package and is not a DAC pin, so the inferred mapping cannot be right as
  stated.
- **The USB CDC-ACM VID/PID this board enumerates as.** `tools/shell_cmd.py`
  inherits `2FE3:0004` from the Giga and that is almost certainly wrong here.
- **Whether a full scratch swap completes**, and how long 1280 KB through the
  QSPI takes.
