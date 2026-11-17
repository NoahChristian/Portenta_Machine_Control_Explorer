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

## 2026-11-15 — The I2C expanders are on i2c3, not i2c1

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

## 2026-11-15 — The SYSCFG analog switches, measured both ways

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
- **Whether AO2 produces a voltage at the terminal at all.** The pin is
  settled — `PG_7` driven by HRTIM, per the datasheet — but Zephyr has
  no HRTIM driver, so nothing in this image can drive it yet. AO0, AO1 and
  AO3 are ordinary PWM and come first.
- **The USB CDC-ACM VID/PID this board enumerates as.** `tools/shell_cmd.py`
  inherits `2FE3:0004` from the Giga and that is almost certainly wrong here.
- **Whether a full scratch swap completes**, and how long 1280 KB through the
  QSPI takes.

## 2026-11-16 — DO1 is the terminal silkscreened 01

The boot indicator drives `MC_DO_DO1_PIN PH_9` through the shared latch
`PB_2`. With the board powered and the panel healthy it blinks, and the LED
that blinks is the one above the terminal marked **01** on J6 — observed
directly, not read out of a header.

That settles the only thing about the digital-output table that was actually
in doubt. The channel-to-pin wiring is PCB traces, so `pins_mc.h` is recording
hardware rather than choosing it; what could not be read off a header was
whether mbed's `DO0..DO7` line up with the connector's `00..07` or sit one
apart. They line up.

So `DO1` is `measured` and the other seven are ordered by construction from
it. They are still `pinout` in tools/terminals.csv, because one observation is
one observation — `d1 walk` lights all eight in turn and would settle the
rest in about ten seconds, which is worth doing before the serial port is
removed.

Observed state at the time:

```
fw 0.34.0  slot0 confirmed   D1 steady at power-up, blinking after the first
                             HTTP request satisfied the panel health check
```

The steady-then-blinking transition is itself the health gate made visible:
steady means a check is still outstanding, and the one outstanding after a
power cycle is "nobody has loaded the panel yet" — which waives itself after
180 s so an unattended board still confirms its image.

## A stalled OTA, four wrong explanations, and a lying exit code (2026-11-16)

**READ THE RETRACTION FIRST.** An earlier version of this section claimed the
panel's polling stops the board being updated, on the strength of one trial
each way. That claim is withdrawn. It may still be true; it is not
established, and the difference matters.

### What the failures actually were, at least some of the time

`ota.ps1` threw "upload failed" whenever smpmgr exited non-zero. smpmgr exits
non-zero **after a transfer that completed**. Run by hand, the same upload
that the script had just called a failure showed:

```
build/app/zephyr/zephyr.signed.bin ----- 100.0%  1.0/1.0 MB  59.3 kB/s
```

and `image state-read` then showed `slot1 = 0.57.0`, hash validated. The bytes
had arrived. The tool fell over afterwards and the wrapper reported it as a
transport failure.

The script's own `-Resume` comment had said this for as long as it has
existed. It was read, quoted, and then not believed when it mattered, because
every reading afterwards was taken through an exit code that does not mean
what it says.

### The theories that were built on that signal

| theory | how it was tested | verdict |
|---|---|---|
| not enough processor | sweep rate vs predicted | **wrong** - 0.82/s against 0.86 |
| RS485 interrupt storm | the receive counter | **wrong** - `rxTotal` 0 over 3 s |
| WiFi power save | mode read back from the radio | **real**, now `0` - did not fix it |
| the panel's own polling | tab open vs closed | **unproven** - one trial each |
| shared receive pool | the radio's own drop counter | **wrong** - 2065 packets, 0 drops |

The drop counter is the strongest of these: it read **zero at the moment an
upload failed**, not merely under synthetic load. Packets were arriving. The
receive pool was never the problem.

Two patterns that looked solid and were not: "it fails at high uptime" (it
later failed 25 seconds after a reboot) and "a reset fixes it" (it later
failed immediately after one). Both were small-sample variance read as law.

### What was changed

- `ota.ps1` now asks the board before declaring an upload failed, instead of
  trusting the exit code. A slot reporting a *version* is not evidence: the
  MCUboot header lands in the first few kB, so a part-written slot still names
  a version. Only a validated image counts.
- `CONFIG_MCUMGR_TRANSPORT_NETBUF_COUNT` 2 -> 4, as an experiment rather than
  a fix. Still untested against the corrected signal.
- The panel backs off when idle and stops when hidden. Worth doing regardless
  of whether it was ever the cause.

### The lesson worth keeping

**Four explanations were produced before anyone read the error text.** The
first act should have been running the failing command by hand and looking at
its output - which took one minute and settled more than an evening of
reasoning did. Every theory above was testable, and each one that got tested
died immediately. The cost was not in being wrong; it was in choosing to
reason instead of to measure, repeatedly, while a measurement was available
the whole time.

### It is not thread priority, and the priorities were never wrong

This was the first assumption and it is incorrect. The build already orders
things the way anyone would choose:

| thread | priority |
|---|---|
| mcumgr UDP, the SMP receive path | 0, highest preemptible |
| net TCP worker | 2 |
| mcumgr workqueue, which writes flash | 3 |
| AIROC WiFi event task | 4 |
| application threads | 7-8 |
| socket service, where the HTTP server runs | 15, lowest |

The update path is already at the top and the web server already at the
bottom, and it starved anyway.

### THE RECEIVE POOL THEORY WAS TESTED AND IS WRONG

Everything in the next two sections was written before it was measured, and
the measurement refuted it. It is kept rather than deleted because the
reasoning was plausible, was argued at length, and was still wrong - which is
the useful part.

With `CONFIG_NET_STATISTICS_WIFI` the AIROC driver publishes its own drop
counter. It increments on exactly one path: it could not get a net buffer for
an arriving frame, logs "Failed to get net buffer", and discards it. So it
measures receive pool exhaustion directly.

120 requests fired as fast as the client could issue them:

```
before : rxPkts=2439  rxDrops=0
after  : rxPkts=4504  rxDrops=0     +2065 packets, +0 drops, 0 client failures
```

**No drops under load.** Panel traffic does not exhaust the receive pool. The
frames arrive; whatever fails, fails above the driver.

The surviving suspect is the SMP transport's own pool:

```
CONFIG_MCUMGR_TRANSPORT_NETBUF_COUNT = 2
```

Two buffers for the entire update transport. That fits what the pool theory
could not: HTTP is unaffected because it uses different buffers; a small
`image state-read` always works because it needs one buffer briefly; a
megabyte upload fails because it needs sustained turnover; the failure worsens
with uptime, which is what a slow leak looks like; and a reset always cures it,
because the pool is reinitialized.

### What was originally argued, and why it was believable

A shared receive pool, allocated before any of those threads can have an
opinion:

```
CONFIG_NET_PKT_RX_COUNT  16
CONFIG_NET_BUF_RX_COUNT  32 x 128 bytes   ~4 kB of receive data in total
CONFIG_NET_MAX_CONTEXTS  16               one per TCP connection
CONFIG_MCUMGR_TRANSPORT_NETBUF_COUNT  2
```

When a frame arrives the WiFi driver takes an interrupt and must put the bytes
somewhere **before anything has parsed a port number** - so before the stack
could possibly know whether this is a firmware update or a status poll. It
takes a buffer from the one global pool. By the time scheduling could express
a preference, the allocation has already succeeded or already failed, and a
dropped packet never reaches a thread at all.

A browser polling two or three times a second holds buffers and a net context
per connection, each lingering after close. **A priority-0 thread with no
buffer to receive into is still stuck.** Scheduling does not allocate memory.

### Zephyr CAN be told to prefer the update - this build simply does not

The first version of this note claimed the opposite, reasoning that a packet
must be allocated before it can be classified and therefore cannot be ranked.
That is wrong, and it is wrong in a way worth recording because it sounds
convincing.

Zephyr implements multiple receive queues:

```
CONFIG_NET_TC_RX_COUNT       1     <- the default, and what this build runs
CONFIG_NET_TC_NUM_PRIORITIES 16
CONFIG_NET_TC_MAPPING_STRICT y
CONFIG_NET_IP_DSCP_ECN       y     <- on by default
```

> "Define how many Rx traffic classes (queues) the system should have when
> receiving a network packet. The network packet priority can then be mapped
> to this traffic class so that higher prioritized packets can be processed
> before lower prioritized ones. Each queue is handled by a separate thread."

Up to eight queues, each with its own thread and its own priority. And
classification does not need a socket lookup: DSCP lives in byte 1 of the IPv4
header, before anything else is parsed, and `NET_IP_DSCP_ECN` already decodes
it. So the chain runs end to end - DSCP, to packet priority, to traffic class,
to a queue with its own thread.

**One queue is a configuration default here, not a limitation of the stack and
not a property of the radio.** The WiFi part has WMM access categories; this is
not a single-queue MAC.

What genuinely is not available on receive is *reservation*.
`CONFIG_NET_CONTEXT_NET_PKT_POOL` ties a packet pool to a socket, but its help
text says "when **sending** network data" - transmit only. The receive pool
stays common, so queues prioritize *processing* (and therefore how fast
buffers are returned), not allocation. If the pool is fully drained, a
high-priority queue still has nothing to receive into.

The complete lever is therefore two-sided, and neither half is sufficient
alone:

| intent | mechanism | state here |
|---|---|---|
| process the update first | `NET_TC_RX_COUNT` > 1 plus DSCP marking | not done, queue count is 1 |
| stop the panel hoarding | `SO_RCVBUF` via `NET_CONTEXT_RCVBUF` | not enabled |
| give the pools headroom | `NET_PKT_RX_COUNT`, `NET_BUF_RX_COUNT` | at defaults |
| make the panel ask less | adaptive poll, pause when hidden | **done** |

Only the last was needed to clear the symptom, which is why it was done first.
The rest are the real fix and remain open.

### What was done

The page now backs off to one request every 3 s when the serial line is quiet,
returns to 400 ms the moment a byte arrives, and **stops polling entirely when
the tab is hidden**. Both are free when they matter.

Still open, and both cheap: the receive pools are at their defaults with 512 kB
of SRAM on the part, and `MCUMGR_TRANSPORT_NETBUF_COUNT` is 2.

### Why this matters more than it looks

OTA is the mechanism that makes every other mistake on this board
recoverable - there is no serial console on it any more. Until this was found,
that mechanism could be disabled by *looking at the diagnostic page*. It is the
kind of fault only a bench instrument finds, because it requires something to
be watching the board while the board is being updated.
