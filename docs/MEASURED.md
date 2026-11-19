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

## RS485 end to end, and the baud ceiling (2026-11-17)

With `TXP485` jumpered to `RXP485` and `TXN485` to `RXN485`, the board sends a
Modbus RTU frame and receives its own bytes back. The whole path is proven at
once: the UART shifts out, the SP335 drives the pair, the receiver recovers
it, the ISR fills the ring, and `/rs485.json` returns it.

```
sent 01 03 00 00 00 01 84 0A   ->   rx 8 : 010300000001840a
```

### Two control pins were inverted, and the sweep settled four

Polarity was established by experiment, not by reading a pin list. With the
jumper fitted, every control line was driven directly and the same frame sent
each time:

```
rs232 hf de | rx | hex
------------+----+------------------
0     0  0  |  1 | 00
0     0  1  |  0 |
1     0  1  |  8 | 010300000001840a   <- the frame, exactly
1     1  1  |  8 | 010300000001840a
```

| pin | assumed | measured |
|---|---|---|
| `/RE` receiver enable | active high | **active LOW** - the board was deaf all session |
| protocol select | low = RS485 | **HIGH = RS485**; low selects RS232 |
| `DE` driver enable | active high | active high, correct |
| `EN` transceiver enable | active high | active high, correct - `en=0` gives no bytes at all |

Driven low "for RS485", the transceiver sat in RS232 and nothing ever reached
the 485 pair, which is why a transmitter that was working perfectly produced a
receiver that heard nothing.

**This needed a diagnostic, not more reasoning.** `485tx` asserts `DE` itself,
so it can never test whether asserting `DE` is correct - the test and the
assumption are the same line of code. A `485raw` verb that shifts bytes and
touches no control pin, plus a `485pin` verb to drive each line, separated
them and answered in seconds what argument had not.

### The baud ceiling, verified by data rather than by configuring

Each rate set over the API and checked by whether the frame survives:

```
The link's real ceiling: 1 Mbaud intact, corruption from 1.5 M,
peripheral refuses above 6 M.

┌──────────────────┬─────────────────────────────────────────┐
│       baud       │                 result                  │
├──────────────────┼─────────────────────────────────────────┤
│ 9600 → 1,000,000 │ INTACT, all 8 bytes                     │
├──────────────────┼─────────────────────────────────────────┤
│ 1,500,000        │ corrupt — 7 bytes, 4020000001840a       │
├──────────────────┼─────────────────────────────────────────┤
│ 2–6 M            │ progressively worse                     │
├──────────────────┼─────────────────────────────────────────┤
│ 8 M and above    │ peripheral refuses to generate the rate │
└──────────────────┴─────────────────────────────────────────┘

So 20 Mbps is the SP335's figure, not this link's — UART4 can't
clock past about 6 M.
```

The refusals come from two different places, which is the point of having
handed the ceiling to the hardware:

```
 6000000 : accepted, turnaround 3 us
 7000000 : uart_configure refused 7000000 8N1 (-134)   <- the peripheral
20000000 : uart_configure refused 20000000 8N1 (-134)  <- the peripheral
25000000 : refused: baud 25000000 outside 1200..20000000  <- our range check
```

`-134` is -ENOTSUP from the STM32 driver. 20 Mbaud was requested and the
hardware declined it; the range check only fires above 20 M, which is why
25 M reads differently.

**1 Mbaud is the verified working limit**, 52x the Modbus-typical 19200.

The SP335 is specified to 20 Mbps; that is the transceiver's figure and not
this link's. UART4 cannot clock past roughly 6 M on this board.

And the corruption at 1.5 M is probably OURS. The tail of the frame comes back
correct (`01 84 0a`) while the head is lost - bytes dropped at the start, not
uniformly scrambled. The receive ISR reads ONE byte per interrupt with
`uart_fifo_read(dev, &b, 1)`, and at 1.5 M a byte arrives every 6.7 us. Fixing
that before blaming the hardware is the honest next step, if anyone ever needs
more than 1 Mbaud here - which no industrial bus will.

Caveat worth keeping: this is a six-inch jumper with no termination. Real
RS485 at these rates is limited by cable length and reflections, not by either
end of this test.

## The radio has its own queues, and Zephyr's priorities never reach them

**2026-11-17. This is a source trace, not a bench measurement.** It is in this
file because it corrects a conclusion recorded here, and it names the number
that would confirm or refute it. Nothing below was observed on hardware.

The prompt was an observation worth more than the four theories that preceded
it: a radio may not process low priority UDP when other traffic is present, so
thread priority and radio priority are not the same thing, and the physical
layer may be behaving differently from the scheduler.

That is correct, and the chain can be read end to end in the shipped sources.

### Zephyr's packet priority is discarded at the driver boundary

```c
/* zephyr/drivers/wifi/infineon/airoc_wifi.c - airoc_mgmt_send() */
static int airoc_mgmt_send(const struct device *dev, struct net_pkt *pkt)
{
    ...
    net_pkt_read(pkt, data->frame_buf, pkt_len);      /* payload only */
    ...
    ret = whd_network_send_ethernet_data(airoc_if, (void *)buf);
```

The payload is copied out and handed to WHD. `net_pkt_priority(pkt)` is never
read. **No Zephyr transmit traffic class, and no thread priority, survives this
call.** Whatever ordering the stack applied upstream ends here.

### But WHD reads the DSCP byte off the wire and queues by access category

```c
/* modules/hal/infineon/whd-expansion/WHD/COMMON/src/whd_cdc_bdc.c */
#define IPV4_DSCP_OFFSET  (15)       /* :55  - byte 1 of the IPv4 header */

dscp = whd_buffer_get_current_piece_data_pointer(whd_driver, buffer)
       + IPV4_DSCP_OFFSET;          /* :440 - read from the frame itself */
if (*dscp != 0)
    priority = whd_map_dscp_to_priority(whd_driver, *dscp);   /* :477 */
```

```c
/* modules/hal/infineon/whd-expansion/WHD/COMMON/src/whd_sdpcm.c */
#define AC_QUEUE_SIZE 64                                           /* :63  */
static const uint8_t prio_to_ac[9] = {1, 0, 0, 1, 2, 2, 3, 3, 4};  /* :157 */

ac = prio_to_ac[prio];                                             /* :634 */
if ((header_type == DATA_HEADER) && (sdpcm_info->npkt_in_q[ac] > AC_QUEUE_SIZE))
{
    result = whd_buffer_release(whd_driver, buffer, WHD_NETWORK_TX);
    ...
    return WHD_BUFFER_ALLOC_FAIL;                                  /* :636 */
}
```

and the drain loop takes the highest category first:

```c
for (ac = MAX_WMM_AC; ac >= 0; ac--)                               /* :512 */
```

So the radio layer **does** prioritize, it has five queues, each is 64 packets
deep, it **drops on overflow**, and the only thing that selects a queue is the
DSCP byte in the packet. Not the sending thread. Not the socket. The byte.

### What this build therefore does

`CONFIG_NET_CONTEXT_DSCP_ECN` is not set in `app/prj.conf`, no socket sets
`IP_TOS`, and so every frame this board transmits carries DSCP 0.

```
DSCP 0  ->  priority 0  ->  prio_to_ac[0]  ->  AC 1
```

**Every packet the board sends shares one 64-deep queue.** The small SMP
responses that acknowledge each upload chunk sit in the same FIFO as the large
HTTP responses that carry the panel and its PNG, and when that queue is full
the next packet is released rather than sent.

### Why that would be a stall and not a slowdown

SMP over UDP is lockstep. The host sends a chunk, waits for the response,
sends the next; there is no window and no fast retransmit. Throughput is
therefore `chunk / round-trip`, and a response that is queued behind a page
of PNG lengthens every round trip in the transfer. A response that is
**dropped** is not recovered by the protocol at all - it becomes a client
timeout, which is exactly the shape of the reported symptom.

Both halves fit the observations: the transfer is slow because the round trip
is long, and it stalls because one round trip never completes.

### This narrows a conclusion recorded above, and it was read wrongly

The receive pool section concluded from `rxDrops=0` that "every buffer theory
on this bench is wrong". That is too broad. `rxDrops` is `errors.rx`, which
the AIROC driver increments on one path, in one direction: it could not get a
buffer for an **arriving** frame. A zero there clears the receive pool and
nothing else. It says nothing about a transmit queue in the radio driver.

The counter for that path is `errors.tx`, incremented by `airoc_mgmt_send()`
on precisely the `WHD_BUFFER_ALLOC_FAIL` return shown above - and it has been
published in `/snapshot.json` as **`txErrs`** the whole time, beside the
`rxDrops` that was read instead. The measurement was already on the board. The
wrong field was read from it.

### The measurement that settles it

Read `txErrs` before and after an upload, with the panel open and with it
closed. It needs no new code and no new Kconfig.

- `txErrs` climbs while an upload stalls -> the access-category queue is the
  cause, and the fix is below.
- `txErrs` stays at zero -> this trace is a correct reading of the sources and
  still not the cause, and the next suspect is the air itself: retries and
  rate fallback, visible through `whd_wifi_get_counters()` and the RSSI, which
  nothing on this board reads yet.

### The lever, if it is confirmed

Mark the management traffic so it stops sharing a queue with bulk traffic:

```
CONFIG_NET_CONTEXT_DSCP_ECN=y
```

then `setsockopt(fd, IPPROTO_IP, IP_TOS, ...)` on the SMP socket. The chain
from there is already complete and was checked: `sockets_inet.c:3057` gates
`IP_TOS` on exactly that symbol, `net_context.c:1338` moves it onto the packet,
`ipv4.c:101` writes it into the header, and WHD reads it back out at byte 15.

**The obstacle is that the transport owns its socket.**
`zephyr/subsys/mgmt/mcumgr/transport/src/smp_udp.c:211` calls `zsock_socket()`
and never calls `zsock_setsockopt()` for anything but DTLS, and exposes no
hook. Reaching it means patching that transport, or adding the Kconfig option
upstream - which is a reasonable thing to want in general, since a device
management transport losing to a web page is not specific to this board.

Checked before recommending, because this is the same shape as the power save
call that compiled, looked right, and returned `-ENOTSUP`: an `IP_TOS` set
without `CONFIG_NET_CONTEXT_DSCP_ECN` falls through to an error return rather
than silently doing nothing, so that one at least fails loudly.

### What it does not explain

One upload failed at 25 seconds of uptime with no browser open anywhere. No
panel traffic means nothing to fill AC 1, so this mechanism does not cover that
case. Either there is a second cause, or that frame was lost on the air. It is
recorded here unexplained rather than folded into the theory.

## The stalled OTA is frame loss on this board's radio link (2026-11-17)

**Measured, and it supersedes every software explanation above.** Four theories
in this file blamed processor load, power save, the panel's polling and the
receive pool. A fifth, added this morning, traced the radio's access-category
queues through the vendor sources. All five were looking for a software cause
for a hardware fault.

Instrument: `ping` from the bench PC, which needs nothing on the board and
cannot be confused by anything in the application.

### Loss rises smoothly with frame length

```
ping -n 15 -w 1200 -l <size> <board>
```

run for each size in turn, the loop's own summary:

```
    32 bytes :   0% loss   avg 62 ms
   200 bytes :   6% loss   avg 70 ms
   500 bytes :  20% loss   avg 78 ms
   800 bytes :  53% loss   avg 59 ms
  1000 bytes :  73% loss   avg 77 ms
  1200 bytes :  53% loss   avg 100 ms
  1472 bytes :  53% loss   avg 80 ms
```

and the two runs that prompted the sweep, `ping`'s verbatim summary lines:

```
    Packets: Sent = 50, Received = 45, Lost = 5 (10% loss),
    Minimum = 4ms, Maximum = 153ms, Average = 56ms        <- 32 bytes

    Packets: Sent = 50, Received = 8, Lost = 42 (84% loss),
    Minimum = 54ms, Maximum = 115ms, Average = 75ms       <- 1400 bytes
```

**No cliff at any particular size**, so this is not the MTU and not
fragmentation. A smooth rise with frame length is what a marginal radio link
does: a longer frame occupies the air for longer, so at a given error rate the
chance of at least one bad bit rises with length.

The round trip is the second tell. A 32-byte ping to a device on the same
subnet averaged **62 ms**, where a healthy 2.4 GHz link gives single-digit
milliseconds. The frames that do arrive are arriving after link-layer retries;
the ones that do not have exhausted the retry limit.

### It is this board, not this bench

The control that matters, same AP, same room, same minute, same 1400-byte
frame:

| target | loss | average |
|---|---|---|
| this board | **73%** | 145 ms |
| a sibling WiFi board on the same bench | 0% | 110 ms |
| the gateway | 0% | 39 ms |

Another board of the same radio family, associated to the same access point,
loses nothing. So the access point is fine, the channel is usable, and 2.4 GHz
congestion is not the explanation. **The fault is in this board's own RF
path** - antenna, its connector, or the assembly between the module and the
carrier's SMA.

Which of those it is has NOT been established. It is written here as the
remaining question, not as a finding.

### Why every software theory fit, and why none of them worked

A crippled antenna still associates. Association, authentication and small
management frames are short, robust and sent at low rates, so the board joins
the network, answers a ping, and serves a page. Only sustained transfer of
full-size frames fails. The board therefore looks healthy in every way a
person checks first.

Specifically, this accounts for each observation that misled the sections
above:

- **`image state-read` always worked; a megabyte upload never finished.** The
  first is one short exchange; the second is hundreds of full-size frames at
  70-80% loss.
- **SMP over UDP is lockstep** - one request outstanding, no window, no fast
  retransmit. A lost frame is not recovered by the protocol, it becomes a
  client timeout. Loss therefore shows up as a stall rather than as a
  slowdown, which is exactly the reported symptom.
- **59 kB/s** when a transfer did run: round trips of 60-150 ms with
  retransmissions, in a protocol whose throughput is one chunk per round trip.
- **`rxDrops=0` and `txErrs=0`, measured.** Both counters are blind to this by
  construction. A frame lost on the air never reaches the driver, so there is
  no buffer to fail to allocate and no queue to overflow. Reading zero from
  both and concluding "not a buffer problem" was correct; concluding anything
  about the cause was not.
- **An upload failed at 25 seconds of uptime with no browser open.** The
  access-category theory could not explain this one and said so. Loss
  explains it without effort, because loss does not care what else is running.
- **The panel-open versus panel-closed trial.** At 73% loss on full-size
  frames, whether an upload completes is close to a coin flip. One trial each
  way could not have meant anything. The retraction was right, for a better
  reason than the one given for it.
- **HTTP that took 0.13 s, 0.44 s, 4.4 s and 10 s** for the identical request
  on an idle link, with power save read back as off. Retransmission, not load.

### The lesson, which is not a new one here

Every one of the five theories was a reading of source code or a plausible
mechanism, and each was argued before it was measured. The measurement that
settled it took four minutes, needed no firmware, and could have been run on
the first evening. **When a network symptom is being explained, measure the
network before reading the stack** - and run a control on a second device
before concluding anything about the first, because a single device's numbers
cannot distinguish a bad device from a bad environment.

### Still to measure

- The RSSI, which nothing on this board reads yet. `whd_wifi_get_rssi()` is
  reachable through the interface handle already exposed for the power save
  readback, so this is the same small addition, and it would give a number to
  compare before and after anything is changed physically.
- The WHD retry and failure counters via `whd_wifi_get_counters()`, which say
  how hard the link is working for the frames that do succeed.
- The same ping sweep over Ethernet, which shares none of the RF path and
  would confirm the application and the OTA machinery are sound.

## The radio is fine, and the frame-loss conclusion is retracted (2026-11-17)

The section above concluded, from a ping sweep, that this board's own RF path
was at fault. **That conclusion is withdrawn.** The signal strength is now
published, and it says the opposite.

### What the radio actually reports

Read through `NET_REQUEST_WIFI_IFACE_STATUS`, which the AIROC driver does
implement, and published in `/state.json` and on the panel:

```
rssi -45 dBm   rate 144.0   chan 3   mode 802.11n   state completed   psMode 0
```

**-45 dBm is a strong signal and 144 Mbps is the ceiling** for 1x1 802.11n at
20 MHz with a short guard interval. The radio has not fallen back one step. No
antenna fault produces those two numbers together.

### And the loss is gone

The same sweep that measured 0 to 73% loss re-run afterwards:

```
size      loss   avg
    32 B    0%     9 ms
   200 B    0%    10 ms
   500 B    0%    21 ms
   800 B    0%    13 ms
  1000 B    0%     9 ms
  1200 B    0%     8 ms
  1472 B    0%    12 ms
```

Flat, at every size, with the round trip down roughly sixfold. `rxDrops` and
`txErrs` both stayed at 0 throughout a 40-packet 1400-byte flood.

### The control was not a control, and that is the lesson

The earlier conclusion rested on one comparison: another board on "the same
access point, same channel, same minute" losing nothing. **That board's band
and channel were never checked.** If it is on 5 GHz, the comparison says
nothing at all about 2.4 GHz conditions, and ambient interference on channel 3
would explain every observation - including why the loss was real when
measured and absent an hour later.

A second board is only a control if it is on the same link. Writing "same
access point" was an assumption typed as an observation, which is the same
failure as the panel-polling claim retracted earlier in this file: a sentence
that reads like evidence and is not.

What the loss WAS remains unexplained. It was reproducible across two runs and
a sweep, and it is not reproducible now. The instrumentation to catch it next
time now exists, which is the only durable outcome.

### OTA works now, and the numbers say how badly it was hurt

Same command, same image size, before and after:

```
erase slot1   150 s timeout, no response     ->  OK in about 8 s
upload        0.1% at 7.1 kB/s, abandoned    ->  completes at the default MTU
```

Four OTA cycles ran end to end over WiFi while chasing the Ethernet problem
below. The update path is not fragile; it was starved.

## Ethernet: compiled in, and the MAC will not initialize (2026-11-17)

`&mac` is enabled and the drivers are in the image - `eth_stm32_hal_v2`,
`eth_stm32_hal_common`, `mdio_stm32_hal` and `phy_mii` all build. The
interface still does not come up, and the board says why:

```
eth: {"present":true,"rc":-19,"initRes":-5,"carrier":false,"link":false}
```

`rc -19` is this application reporting the device not ready. `initRes -5` is
**-EIO out of `eth_initialize()` itself**, read back from
`dev->state->init_res`.

### CORRECTION: this board HAS a console, and that error cost four OTA cycles

Everything in this section premised on "no console" is **wrong**, and the
premise shaped the work rather than just one step of it. The board enumerates
**two** Zephyr USB CDC ports (`VID_2FE3&PID_0004`) and runs a full Zephyr
shell at 115200 - `kernel`, `net`, `i2c`, `adc`, `gpio`, `mdio`. The claim was
carried forward unchecked from the MCUboot half, where the console really is
off, and then used to justify publishing driver internals through the web
panel.

One command would have replaced the entire register probe and a round of
hypotheses:

```
net mem
```

So: **check for an enumerated port before concluding a board is mute.** The
technique below is still sound and still worth having - `init_res` is the right
answer when a driver fails before any console exists - but it was built for a
constraint that was not real.

### Reading a driver's init errno on a board with no console

Worth keeping as a technique. Zephyr stores every device's init return value
in `dev->state->init_res` as a POSITIVE errno - the field's own comment says
so, since no Zephyr errno exceeds 255. On an assembly with no console and no
RTT, a driver that fails during init otherwise leaves **no evidence at all**,
and this turns that silence into one number. It ended a round of guessing that
had already produced four plausible and unmeasured explanations.

### Traced as far as source allows

**The claim below was right by luck, and the reasoning was wrong.** -EIO is
returned from TWO places in `eth_initialize()`: `:102` when `clock_control_on`
fails and `:123` when `HAL_ETH_Init` fails, the latter translating the HAL
path's own -EINVAL. The errno alone cannot tell them apart. What settles it is
`gates`, below.

In `eth_stm32_hal_common.c` the -EIO is `HAL_ETH_Init()` failing, and in
`stm32h7xx_hal_eth.c` that function has exactly one realistic failure before
any of its parameter checks: it sets `ETH_DMAMR_SWR` and spins until the bit
clears, returning `HAL_ERROR` on timeout. **That reset cannot complete without
the RMII receive clock**, which on this board is 50 MHz arriving on PA1 from
the PHY - the MCU does not generate it.

### Ruled out, each by test rather than by argument

| suspected | how it was eliminated |
|---|---|
| init order - drivers running before the PHY's reset is released | real, and fixed. `CONFIG_ETH_INIT_PRIORITY` was 60 against a reset regulator at 75. Corrected to 80/81/82 and **verified in the ELF**, not assumed: `check_init_priorities.py --initlevels` shows both fixed regulators ahead of `eth_initialize`. Did not fix it. |
| the PHY needing time to lock after reset | `startup-delay-us = <50000>` on `ethernet_phy_en`, honored by `regulator_fixed.c` with a `k_usleep`. No change. |
| the PA1 analog switch isolating the reference clock | **RETRACTED - this test never ran.** See "The =n path was a no-op" below. The switch was open in both of the runs recorded here. |
| MII selected instead of RMII | the driver passes `.MediaInterface` from `phy-connection-type`, and `HAL_ETH_Init` calls `HAL_SYSCFG_ETHInterfaceSelect(SYSCFG_ETH_RMII)` with a read-modify-write that preserves the analog switch bits. Already correct. |
| the Ethernet clock gates | all three present in the devicetree - `stm-eth`, `mac-clk-tx`, `mac-clk-rx` on RCC 0xd8. |

### The registers, read from the running board (2026-11-17, 0.70.0)

A raw-register probe in `net.c`, run once when the MAC's init fails - safe
only then, because no driver owns any of it:

```
eth: {"initRes":-5,"gates":7,"epis":4,"sw":15,"phyRst":1,
      "swr":1,"phyAddr":0,"phyId":"0007:c131"}
```

| field | reads | and settles |
|---|---|---|
| `gates` | 7 | all three ETH clock gates on, so `clock_control_on` succeeded and the -EIO is the `HAL_ETH_Init` path. Proven, not inferred. |
| `epis` | 4 | RMII selected in `SYSCFG_PMCR`. |
| `phyRst` | 1 | PJ15 high: the PHY's reset is released. |
| `swr` | 1 | **`ETH_DMAMR_SWR` never cleared.** The MAC is still in software reset, which is what the 500 ms `ETH_SWRESET_TIMEOUT` expired on. |
| `phyAddr` | 0 | the PHY answers MDIO at address 0. |
| `phyId` | `0007:c131` | OUI 0007C0 and model 0x13 rev 1: a **Microchip LAN8742A**. |

Two things worth more than the table.

**MDIO works while the MAC is held in reset.** `HAL_ETH_ReadPHYRegister` checks
only the MACMDIOAR busy bit - it never looks at `gState` - so the PHY is
readable after the init failed. One catch: the SWR spin at
`stm32h7xx_hal_eth.c:361` bails *before* `HAL_ETH_SetMDIOClockRange()` at
`:379`, so MACMDIOAR.CR is still at its reset value of HCLK/42, about 5.7 MHz
at this HCLK, well past what the PHY accepts. The probe sets the divider itself
or the read would fail for a reason that has nothing to do with the PHY.

**The failure is between the PHY and the MAC, and nowhere else.** The PHY is
powered, out of reset, internally clocked, identified over MDIO, and the jack's
own LEDs show link solid with activity blinking - all of which need the PHY's
25 MHz. The MAC's DMA reset cannot complete without the RMII receive clock. So
the question is no longer whether the PHY works; it is whether its 50 MHz
REF_CLK reaches PA1.

### The =n path was a no-op, and it faked a completed experiment

`LL_SYSCFG_OpenAnalogSwitch()` is a `SET_BIT`. `pmc_syscfg.c` only ever *set*
switch bits, so `CONFIG_PMC_OPEN_PA1_ANALOG_SWITCH=n` did not close PA1's
switch - it merely declined to open it, leaving whatever the bit already held.
The running board reports `sw` 15: all four switches open, including PA1's, in
a build whose generated `.config` says `# CONFIG_PMC_OPEN_PA1_ANALOG_SWITCH is
not set`. The bit also survives the soft reset that OTA uses, so an earlier
`=y` run leaves it open for every image after it.

So the PA1 experiment recorded as "tested both ways" was never performed. Both
runs had the switch open, and so did the first attempt to repeat it today.

Fixed by making the shield drive **both** directions explicitly and verify
both: the open set must read 1 and the closed set must read 0, with a failure
in either direction returning -EIO. **Not opening a switch is not closing it**,
and this is the second time on this board that a configuration knob which only
ever adds has quietly produced a test that did not run - the first being
`-DCONFIG_FOO=n` persisting in the CMake cache, below.

The lesson is the probe's, not the fix's: the knob, the generated `.config` and
the hardware all have to be read separately. Two of the three agreed and were
wrong.

### "WIFI 1 (802.11b)" is a DERIVED LABEL, not the association mode

Third wrong conclusion from the same investigation, and the cheapest to have
avoided: read the driver.

`airoc_wifi.c:1289` computes `link_mode` from the instantaneous data rate,
under its own comment that this "appears to be the only way to determine the
phy mode with the whd SDK that we're currently using":

```c
if (rate > 54)                               WIFI_4
else if (rate in {6,9,12,18,24,36,48,54})    WIFI_3
else                                         WIFI_1
```

So 11 Mbit reports 802.11b **and so do 39, 43 and 52 Mbit**, because they are
not in that 802.11g set. The link never "fell back to 802.11b"; the label just
means "not above 54 and not a g-rate". The panel now prints it as
`(..., derived)` so it cannot be read as the association mode again.

### What is actually true about the radio

- The PHY TX rate genuinely swings **11 to 104 Mbit** at a steady -43 to -50
  dBm. That value is real - it is WHD's `current_phy_tx_rate`.
- The driver sees **no faults at all**: `rxDrops=0`, `txErrs=0` across 46,893
  rx and 7,023 tx packets. Frames leave cleanly.
- TCP nevertheless resent **711,160 of 4,053,276 bytes - 17.5%** - with 419
  packet drops and 97 connection resets. Retransmission without driver error
  points at the transport or the air, not the SDIO path.
- Power save is off (`wifiPsMode` 0), 24 V is present (`highs=6`,
  `encoderWired=false`), buffers are far from full, no thread is blocked, and
  nothing leaks.
- A 1.7 MB run (12 x 144 KB) completed with zero failures; a 1 MB OTA fails
  about half the time. The path is marginal rather than broken.

**AND THE SIBLING COMPARISON DOES NOT COVER THIS.** The other Zephyr board on
this bench, up 41 h on the same SoC and radio, serves a small JSON document.
This one serves a 144 KB page and 1 MB images. Its uptime is evidence about
association stability at low load and says nothing about throughput - so it is
not a control for the failure being chased. Their TCP and SDIO settings are
identical (both at defaults), and the two differences that did exist - system
heap and workqueue stack - are now matched and did not change the behavior.

### RETRACTED: the radio-loss conclusion below, and the heap theory that followed

Both wrong, and the measurements that killed them are worth more than either.

**The loss figures below are an artifact.** They came from `ping -w 1000`, which
counts a merely LATE reply as lost - and larger frames take longer, which
manufactures a loss-versus-size curve out of nothing. The board's own counters
said so at the time and were not read properly: `ICMP recv` rose by 5 while
`ICMP drop` rose by 12, and IPv4 `recv +67 / drop +45`. Those packets REACHED
the board. A frame lost on the air never reaches a counter.

**The system-heap theory was also wrong.** A sibling Zephyr board on this bench
with the same SoC and radio, up 41 h, sets `CONFIG_HEAP_MEM_POOL_SIZE=32768`
where this app set nothing, and WHD does allocate from the system heap
(`airoc_whd_hal_common.c:70` is `k_malloc`). But:

- `CONFIG_HEAP_MEM_POOL_SIZE` is only a requested MINIMUM. The adds -
  `ADD_SIZE_BOARD=20480`, SDHC 2048, getaddrinfo 280 - already gave ~22.8 KB,
  so `k_malloc` was never failing. That is why WiFi associated at all.
- With `CONFIG_SYS_HEAP_RUNTIME_STATS=y`, `kernel heap` reports peak demand of
  **15,864 bytes** - comfortably inside the old 22.8 KB.
- Twelve full 144 KB page fetches, ~1.7 MB, all succeeded and left
  `max. allocated` **unchanged at 15,864**. WHD does not allocate per packet.

Kept anyway as hygiene, not as a fix. `sysworkq` was at 1536 of 2048 (75%);
at 4096 its high-water is the same 1536 absolute, so it was tight but never
overflowing.

### What that stress run DID establish: the TX pool, with a number

```
RX DATA    14 of 1024 used
TX DATA   184 of 512 used     for ONE 144 KB page
```

**TX peaked at 184 fragments.** The original 32 could not render this page, and
an intermediate 128 would not have either. Sizing to the ceiling rather than to
the last measurement was the right call; incrementing would have failed twice
more. 1.7 MB moved with zero failures at 802.11n / 104 Mbit.

### The variable that actually tracks the failures

The link mode. Uploads failed while `wifi status` read **WIFI 1 (802.11b)** at
43 Mbit; they succeed at **WIFI 4 (802.11n/HT)** at 104 Mbit. RSSI was -43 to
-45 dBm throughout and the antenna never moved, so this is not the RF
environment - the radio drops to a legacy mode and recovers on a reboot.

Both are now published on the panel (`mode` and `rateTenths`), which is what
makes the correlation visible at all. The open question is why it falls back,
and the obvious next step is a watchdog: notice the mode or rate collapsing
while associated, and force a reconnect.

### SUPERSEDED - the radio-loss measurement (2026-11-17)

Power save is genuinely off: `wifiPsMode` reads **0**, read back from WHD after
`whd_wifi_disable_powersave()` rather than assumed. Not sleeping, not
hibernating. The AIROC driver does not implement the get-PS-config op, so
`wifi ps` on the shell answers "Failed to get PS config" - the panel's
read-back is the only place this can be seen.

The link itself had collapsed:

| | earlier the same day | at the time of the failures |
|---|---|---|
| RSSI | -45 dBm | -44 dBm |
| rate | 144.0 Mbit | 43.0 Mbit |
| link mode | 4, 802.11n/HT | **1, 802.11b** |

Loss against frame size, 20 pings each, from the bench PC:

```
   32 bytes   35% loss
  500 bytes   55%
 1000 bytes   50%
 1400 bytes   70%
```

**THE CONTROL, which an earlier attempt at this lacked.** The same 20 x 1400
byte test from the same PC in the same minute to the AP's own gateway: **0%
loss**. So the loss is specific to this board's radio link, not to the network
or the measuring host. Strong signal with heavy loss at every frame size and a
fallback to 802.11b is a noisy channel, not range - channel 3 overlaps 1
through 6 in a crowded 2.4 GHz band, and the CYW4343W has no 5 GHz to escape
to.

That is the whole OTA story: a 1 MB image over a lockstep SMP transport cannot
cross a link losing half its large frames, it dies at a different point each
time, and a retry sometimes gets through. It also explains every symptom
chased in software today - buffers all FREE when wedged rather than exhausted,
every thread pending, no socket or context leak, ping replying while HTTP did
not. A frame lost on the air never reaches the driver, so no counter in the
application can see it.

**One inconsistency, left as observed:** the mode reads 802.11b while the rate
reads 43 Mbit, which is well above 802.11b's 11 Mbit ceiling. The two fields
come from different places in the driver, so one of them is lying; not worth
resolving to act on this.

**What to do about it is not firmware.** Move the AP off channel 3, and use USB
DFU for flashing until the link is sound - DFU is a cable and is reliable.

### The PHY, interrogated over MDIO (2026-11-17)

With `CONFIG_MDIO_SHELL=y` and the MAC still dead, read over USB at PHY
address 0. The divider survives from the probe, so the shell's reads work.

| reg | value | meaning |
|---|---|---|
| 1 BMSR | `0x780d` | **link up** (bit 2), autoneg **not complete** (bit 5 clear), 10/100 capable |
| 4 ANAR | `0x01a1` | we advertise 100 full, 100 half, 10 half |
| 5 ANLPAR | `0x0001` | the partner advertises **no ability at all** |
| 31 | `0xffff` | not a register on this part |

So the wire is linked with nothing negotiated. The panel says exactly that -
"PHY link up, speed unnegotiated" - because `anar & anlpar` yields no speed bit
and rendering the 0 as "0 half" would be a fabricated reading.

**The green lamp on the drawn RJ45 is this link bit, and the amber is a CSS
animation.** The board has no per-packet activity signal to offer, and the real
amber blinks on wire traffic the PHY sees that never reaches this controller,
so the blink is decoration for "live wire here" rather than a measurement. It
is suppressed under `prefers-reduced-motion`.

`BMSR` bit 2 is **latching low**: it reads 0 if the link dropped since the last
read, even once the link is back. `net.c` reads it twice and uses the second
value. A single read makes a recovered link look down.

### DO state, and the LED that proves it

The panel publishes DO state off the GPIO registers, and the per-channel LED
mechanism that already drove DIN and PDIO now drives DO too. Verified by
driving the line directly through the GPIO shell rather than the write path:

```
gpio set gpio@58021000 2 1   ->  "DO3 [PE_2] do on DIGITAL OUTPUTS - ON",  1 LED lit
gpio set gpio@58021000 2 0   ->  "... - OFF",                              0 lit
```

Exactly one lamp, the right one. **No caveat is attached to a lit lamp.** A lit
channel LED means this controller is driving that output; whether 24 V reaches
the terminal is a property of the wiring, not a claim the firmware is making,
and the old hedging text said otherwise.

### The OTA stall is STILL unexplained, and the buffer pools were not it

`net mem` during a failed upload showed RX DATA 0 available with MaxUsed pinned
at its ceiling of 32, which looked conclusive. Fragments are 128 bytes against
a 1544 MTU, so one packet costs 13 of them and 32 held two packets.

Raised to 192/128. It is still the right sizing - measured TX demand alone
peaks at **58**, so the old 32 was genuinely too small and its "30 of 32" was
the ceiling rather than the demand. **But it did not fix the upload.** On the
enlarged pools a 1 MB upload failed with RX DATA peaking at 17 of 192 and TX at
58 of 128, nowhere near exhaustion, at RSSI -45 dBm and 144 Mbps.

So the exhaustion was a **symptom** of the stall, not its cause: when the
transfer wedges, buffers back up behind it. Uploads now succeed roughly every
other attempt and a retry clears it, which fits frame loss rather than
starvation.

Recorded as unresolved for the second time. What is new is that the panel and
`net mem` now instrument it, and that one wrong explanation has been eliminated
by measurement rather than left standing because it was plausible.

### The PHY's control register, and one cause for both symptoms (2026-11-17)

Read and written over the MDIO shell, no reflash. Every value below was read
three times identically, and the ID registers were correct in the same pass,
so the bus is reliable rather than returning noise.

```
reg 0 BMCR   0x0000   autoneg DISABLED, 10BASE-T, half duplex
reg 1 BMSR   0x780d   link up, autoneg capable, autoneg NOT complete
reg 18       0x9b9d
reg 27, 31   0xffff   not registers on this part
```

**`BMCR` 0x0000 is the post-reset strap state, not something software chose.**
`phy_configure_link()` is never called by `eth_stm32_hal_*`, and `phy_mii`'s
init only issues `MII_BMCR_RESET` - so the PHY is left exactly as its straps
leave it. (`phy_mii` IS running, and logs `PHY (0) is down`; an earlier note
here guessed it had never initialized.)

Confirmed by experiment: writing `BMCR` 0x1200 - autoneg enable plus restart -
**dropped the link and it never returned**, with the link partner still
advertising nothing after 16 s. Writing 0x0000 back brought the link straight
up. Restored as found.

**One cause fits all of it.** On the LAN8742A the `nINTSEL` strap selects
REF_CLK In or REF_CLK Out, it is latched at power-on and on the rising edge of
nRST, it **shares the LED2 pin**, and its default is REF_CLK *In* through an
internal pull-up - in which mode the PHY drives no 50 MHz clock. The same LED
pins carry the MODE straps, and mode 000 is exactly 10BASE-T half with autoneg
off.

| observed | follows from |
|---|---|
| `swr` stuck, DMA reset never finishes | no REF_CLK for the reset to clock |
| `BMCR` 0x0000 | MODE strap 000 |
| autoneg enabled loses the link entirely | 000 is not an autoneg mode |
| PHY alive, MDIO fine, jack LEDs lit | the PHY runs off its own 25 MHz crystal |

On this carrier LED1 and LED2 go to the RJ45's lamps, so that LED circuit is
what sets those straps. Per the datasheet the strap is the only control for
REFCLKO - there is no register bit - so the link mode is fixable in firmware
and **the clock is not**.

**Two caveats, because this is a conclusion and not a measurement.** The "no
register" point rests on a forum reading plus a Rev A schematic checklist that
ambiguously names `WUCSR` for LED2's function. And the Portenta H7 ships with
working Ethernet under mbed, which implies the module is normally strapped
correctly. Settle it by scoping PA1 for 50 MHz, or by booting the mbed
`PortentaEthernet` example on this assembly: if Ethernet works there, this
conclusion is wrong and the fault is in software after all.

### RETRACTED - IT WAS NOT HARDWARE, AND THE FIX IS TEN LINES

Everything from "One cause fits all of it" to the end of this section is
**wrong**. The strap theory was right that the straps were wrong, and wrong
about why and about it being unfixable. The LAN8742A re-latches its straps on
every rising edge of nRST, which is PJ15 and ours to drive; pulsing it once
more before the MAC initializes makes Ethernet work from boot at 100 Mbit full
duplex. See "Ethernet works, and the conclusion above was wrong (2026-11-18)"
at the end of this file.

Left in place rather than deleted because the measurements in it are sound and
only the conclusion drawn from them was not. The fix is what matters here: a
working 100 Mbit interface beats a well-documented broken one. The record of
how the wrong conclusion was reached is kept because it is cheap to keep, not
because it is the valuable part.

### What was thought to be left, and thought to be hardware

Three questions this bench cannot answer from software, and the next step is a
scope on PA1 or the carrier schematic:

- is PJ15 actually the PHY reset on THIS carrier, or only on the board Zephyr's
  portenta_h7 support was written against?
- does the PHY have its own clock source, and is it gated by something?
- is the PHY strapped to drive REF_CLK out at all?

**Reported as unresolved.** The interface reports itself honestly - present,
not ready, with the driver's own errno - rather than looking like a cable
problem. The shield overlay previously disabled `&mac` with the note that "an
interface nothing has ever exercised is not a feature"; it has now been
exercised, and it does not work.

### Two gotchas found on the way

- **`-DCONFIG_FOO=n` persists in the CMake cache.** After the PA1 experiment, a
  normal rebuild still produced `# CONFIG_PMC_OPEN_PA1_ANALOG_SWITCH is not
  set` despite `default y`. Only `-Pristine` cleared it. A one-off Kconfig
  override silently becomes the new default for every later build in that
  directory - and the board ran one cycle with AI2 shorted to the RMII clock
  because of it. Check the generated `.config` after any build whose
  configuration matters, which is the only reason this was caught.
- **`check_init_priorities.py` fails the build on a devicetree ordering
  violation.** The first attempt at the priority fix put MDIO ahead of the MAC;
  the build refused it, naming both symbols, because the mdio node is a CHILD
  of ethernet@40028000 and therefore depends on it. A gate that catches a wrong
  init order at build time is worth knowing about on a board that cannot report
  one at runtime.

## The OTA stall, caught on the console (2026-11-18, 0.88.0)

This answers "The OTA stall is STILL unexplained" above.

Method: the logging build on the board, the console streamed to a file, then
`smpmgr image upload --slot 1` repeatedly with **no reset between attempts**,
until one failed. Uploads only, so nothing swapped and nothing rebooted.

```
attempt 1: exit=0 elapsed=17s
attempt 2: exit=0 elapsed=16s
attempt 3: exit=1 elapsed=98s     <- caught
```

17 s for 1,129,310 bytes is about 70 kB/s. The client's account of the
failure, verbatim:

```
[04:48:54] ERROR  Timeout (90.0s) waiting for request      __init__.py:233
           ERROR  Connection to device lost:        image_management.py:241
                  TimeoutError - Timeout (90.0s)
```

### The board's side: two threads stop, nothing else does

Last board-clock timestamp per thread, from the capture:

```
thread            lines   first     last   silent
smp_udp4          11802   86.00   125.91    96.48
mcumgr smp         1725   86.05   125.92    96.48
rx_q[0]           33926   77.35   222.39     0.00
WHD               16261   77.66   222.39     0.00
http_server_tid    2601   79.31   221.25     1.14
net_socket_service 1137   80.42   221.38     1.02
```

Both SMP threads go quiet in the same millisecond and stay quiet for 96 s while
the radio, the receive queue and the web server carry on. **Zero errors and
zero warnings** anywhere in that window.

### It is NOT a wedge, and the thread state says so only if read carefully

`kernel thread list` on the stalled board reports both as
`state: pending, timeout: 0`. That looks like a deadlock and is not: it is the
normal idle state of a thread blocked in `recvfrom(K_FOREVER)`. Compare
`http_server_tid`, also `pending` but with `timeout: 748`, which is a poll with
a deadline.

Proof rather than inference: an SMP `image state-read` **answered normally**
minutes later with no reset. The threads were idle, not stuck. They had nothing
to do because the client, having lost one reply, sends nothing further while it
waits out its own 90 s timeout - so the silence is on both sides and is
expected.

### It is NOT resource starvation either, measured while stalled

```
TX DATA   320 total, Avail 320, MaxUsed 32
RX DATA   256 total, Avail 256, MaxUsed 16
heap      free 117240, allocated 13792, max. allocated 15864
stacks    smp_udp4 712/1024 (69%), mcumgr smp 1336/2048 (65%)
```

Every TX buffer free, the peak a tenth of the pool, the heap byte-identical to
idle, no stack over 69%.

### So the failure is ONE LOST DATAGRAM, in a protocol with no retransmission

SMP over UDP is lockstep with no window. One datagram lost in either direction
ends the transfer; there is nothing in the protocol to resend it.

**The rate, by arithmetic.** The image is roughly 1,100 datagrams and about
half of attempts succeed, so `(1 - p) ** 1100 ~= 0.5` gives **p ~= 0.06%** -
about one datagram in 1,600.

That number explains two things that had been read the other way:

- **Why a small ping test said "no loss."** 80 ICMP round trips at 64, 512,
  1024 and 1400 bytes lost nothing, and that was briefly treated as
  exonerating the link. At 0.06% per datagram, 80 samples have about a 5%
  chance of seeing a single drop. The test was not sensitive enough to detect
  the thing that breaks the transfer.
- **Why `--mtu 256` made it worse rather than better.** Under per-datagram
  loss, a smaller MTU multiplies the number of lockstep exchanges, so four
  times the datagrams is four times the chance of losing one. An `--mtu 256`
  attempt failed where the default had succeeded minutes earlier, which fits
  per-datagram loss and not per-byte loss. The `-Mtu` comment in
  `tools/ota.ps1` claimed size-dependent loss and recommended shrinking frames
  on that basis; it now carries the retraction and says the option is for
  testing the model, not for fixing the link.

**The contrast that names the missing property.** Same radio, same window:

```
TCP bytes recv 151219  sent 492567  resent 355    TCP seg drop 0
UDP recv 2475  sent 1930                          (no resend exists)
```

TCP lost segments too and recovered by resending them. SMP cannot.

### Ruled out by measurement, each of them previously asserted

| claim | how it died |
|---|---|
| WiFi power save buffering frames at the AP | isolated pings 6 s apart answered in 3 ms, 8 of 10 |
| gross size-dependent frame loss | 0 lost of 80, payloads 64 to 1400 bytes |
| the radio not receiving during the stall | 16,261 WHD receive lines **after** the transfer died |
| an mcumgr deadlock | SMP answered minutes later, no reset |
| buffer, heap or stack exhaustion | the numbers above, all at idle levels |

The AIROC driver turned out to contain no power-save handling at all - no
`whd_wifi_enable_powersave`, no `listen_interval`, no `get_power_save` op,
which is why `wifi ps` answers "Failed to get PS config". So power save was
never configured either way, and the ping timing shows the radio awake.

### WHICH datagram was lost: the board's reply, and it left the stack

The last thing either SMP thread did was a completed transmit. Trimmed to the
control path, with addresses removed:

```
<dbg> net_pkt.net_pkt_skip:        (mcumgr smp): pkt 0x2405e838 skip 8
<dbg> net_if.net_if_tx:            (mcumgr smp): Processing (pkt 0x2405e838 ...)
<dbg> net_arp.arp_entry_find:      (mcumgr smp): found dst <the client>
<dbg> net_arp.net_arp_prepare:     (mcumgr smp): ARP using ll <the client>
<dbg> net_ethernet.ethernet_send:  (mcumgr smp): Found ARP entry, sending pkt
<dbg> net_ethernet.ethernet_fill_header: (mcumgr smp): src <board> dst <client>
<dbg> net_pkt.net_pkt_read:        (mcumgr smp): pkt ... length 61
<dbg> net_if.net_if_tx:            (mcumgr smp): Calling context send cb status 61
```

ARP was already resolved, the Ethernet header was filled, 61 bytes were read
out of the packet for the driver, and `net_if_tx` called the context send
callback with a **non-negative** status. Nothing in that sequence failed.

And `smp_udp4` logged nothing afterwards, which means no further request
arrived - which is exactly what a lockstep client does while it waits for a
reply it never got.

So the missing datagram is the **board's outbound reply**, and it was lost at
or beyond the radio: after `net_if_tx` handed it to the WiFi driver. The
network stack did its job. That narrows the remaining suspects to the WHD
driver, the 4343W firmware, or the air itself - the board-to-AP direction.

### Still open

- Where in the board-to-AP path the reply dies: the WHD SDIO handoff, the
  radio firmware's own queue, or the air. The driver reports no error, and
  `whd_bus_sdio_sleep`/`wakeup` were already found to be no-ops.
- `Processing err 692` on the WiFi interface is unexplained.
- The board's UDP drop counter (2,256) does not help separate any of this: it
  is dominated by ambient traffic with no listener - mDNS to 5353, a foreign
  DHCP conversation, and 0x88ca frames - because the broadcast domain is a /22.

### The fix, measured: the same image over TCP

`POST /ota` writes the signed image into the secondary slot through the same
`flash_img` API `img_mgmt` uses, over the panel's existing TCP server.
Same board, same radio, same session in which five consecutive UDP attempts
had just failed:

```
upload over HTTP/TCP   1,130,642 bytes in  4 s   260 kB/s
upload over HTTP/TCP   1,130,720 bytes in 14 s    80 kB/s
upload over SMP/UDP    1,129,310 bytes in 16 s    70 kB/s   (when it worked)
```

Then end to end on the new path: staged, armed with `do=otatest`, reset,
MCUboot swapped, and the health gate confirmed the new image on the fourth
15-second poll. The byte count the board reported matched what was sent
exactly, which is what the uploader checks before it will arm anything.

### A 32-character token in a 32-byte buffer (2026-11-18)

The first upload transferred perfectly and was refused for a missing token.
`CONFIG_HTTP_SERVER_MAX_HEADER_LEN` sizes **one** buffer that the HTTP/1 parser
reuses for a header's name and then its value:

```c
if (offset + length > sizeof(ctx->header_buffer) - 1U) {
        LOG_DBG("Header %s too long (by %zu bytes)", "value", ...);
        ctx->header_buffer[0] = '\0';
}
```

At the default of 32 the usable value is 31 characters. The API token is 32, so
it needed 33 bytes, was discarded at **debug** level, and the header was never
captured - `POST /ota` saw no token and refused every upload. The registration
macro's `BUILD_ASSERT` checks only the NAME (12 bytes for `X-PMC-Token`), so
the build was clean. Raised to 64.

Two things worth keeping from how it was found:

- **The client was tested in isolation first.** A loopback `HttpListener`
  showed `Invoke-WebRequest` putting `X-PMC-Token` on the wire correctly, which
  moved the fault to the board without touching the board. Guessing between two
  ends is what burns flash cycles.
- **The first diagnostic was logged to the console and never arrived.**
  Deferred logging dropped it under the `net_pkt` flood - the same mechanism
  that discarded 781,997 messages above. The refusal now carries its reason in
  the HTTP response body, because the caller is the one party guaranteed to be
  listening.

### DTLS does not address this

Zephyr's `smp_udp` does support it: `CONFIG_MCUMGR_TRANSPORT_UDP_DTLS` opens
`NET_IPPROTO_DTLS_1_2` with a credential sec tag. But smpmgr 0.20.2 has no
client-side option for it (`--ip --port --ble --timeout --mtu --baudrate
--line-length --line-buffers` is the whole list), and more importantly DTLS
adds no retransmission - it adds a handshake and about 29 bytes per record, so
under per-datagram loss it makes a transfer longer and slightly more fragile.
The property this failure needs is retransmission, which means TCP, and this
Zephyr has no TCP transport for SMP (`smp_bt`, `smp_dummy`, `smp_lorawan`,
`smp_raw_dummy`, `smp_raw_uart`, `smp_shell`, `smp_spi`, `smp_uart`,
`smp_udp`).

## Thread stacks on 0.98.0, and a wrong-board reading on the way (2026-11-18)

Taken after the pulse thread had actually run, so its figure is a high-water
mark and not an unused stack. `CONFIG_INIT_STACKS` and `THREAD_MONITOR` make
the unused count real rather than an estimate.

```
net_mgmt             568 / 832     68%      <- the tightest
net_socket_service  1024 / 1536    66%
rx_q[0]             1016 / 1792    56%      was 80% in the logging build
pmc_clock           1720 / 3072    55%      the 2048 it outgrew would be 84%
usbd                 548 / 1024    53%
mcumgr              1040 / 2048    50%
pmc_pulse            208 / 1024    20%      new; 1024 is generous and stays
http_server_tid     1504 / 8192    18%
```

Nothing above 68%, and `pmc_clock` at 55% of 3072 confirms the earlier raise:
1720 bytes would have been 84% of the 2048 it used to have, against a
measured 99% on the build that prompted it.

**THE READING BEFORE THIS ONE WAS ANOTHER BOARD'S.** The first capture walked
the COM ports in order, took the first one that answered `kernel thread list`,
and reported 19 threads - `net_thread`, `http_server_tid`, `smp_udp4`, `WHD`,
`airoc_event`, `net_socket_service` at 88%. Entirely plausible, and entirely
about a different project: COM21 is another Zephyr app on this bench with the
same CDC-ACM shell and the same subsystem thread names.

The tell was an absence. No `pmc_*` thread appeared, which first read as a
truncated capture - the window was lengthened from 3 s to 10 s, the list came
back at the same 19, and only then did the absence mean what it meant.

`tools/ota.ps1` has guarded against exactly this since it was written: it asks
`mdnsname` and skips any port that does not answer for this project, because
a mis-targeted update writes another board's flash. The lesson generalizes
past flashing - **a measurement from an unidentified port is a measurement of
an unidentified board** - and a reading that looks reasonable is the kind that
gets believed. Identify first, then measure.

## The upload verification that could not fail (2026-11-18)

`tools/ota.ps1` asked the board whether the bytes had arrived - the right
instinct, and the whole reason the UDP path was usable at all - and then
accepted this as the answer:

```powershell
if ($after -match "0\.\d+\.\d+" -and $after -match "slot=1") {
```

Neither half is a check. `0\.\d+\.\d+` matches any version anywhere in the
output, **including slot0's own**, so it passes on a board that received
nothing; and `slot=1` is the literal text of a block label that
`image state-read` prints whether or not slot1 holds an image. A clean pass
meant "the board answered", not "the image is there". The script then marked
whatever was in slot1 pending and reset into it. `-Resume` was worse: it
skipped the question entirely, on the word "assumed", on the one path that
exists *because* an upload ended ambiguously.

**The fix is to compare the hash, and it is computable locally.**
`bootutil_img_hash()` digests the header (`ih_hdr_size`), then `ih_img_size`
bytes of payload, then `ih_protect_tlv_size` bytes of protected TLVs - and
stops short of the unprotected TLV area that carries the signature and the
digest itself. For `0.94.0` that is 1,130,568 bytes of a 1,130,720-byte file.

Verified three ways before being trusted, none of which needed a flash cycle:

```
local SHA-256 over hdr+img+prot   F47DE115AA36FC97...   (PowerShell)
the image's own SHA256 TLV        F47DE115AA36FC97...   (imgtool dumpinfo)
slot0's hash, running that image  F47DE115AA36FC97...   (image state-read)
```

The naive version is the trap: SHA-256 of the **whole file** is
`230ADB9CBC1F63DC...`, which matches nothing the board will ever report. A
comparison that can never succeed is worse than none, because it calls a good
transfer a failure.

**Then the new check was proved on the board, with no upload and no reset.**
slot1 still held `0.93.0` from the previous swap, so `-Resume` was run against
exactly the state the old code mishandled:

```
image        0.94.0, 1,130,720 bytes
  slot0 0.94.0   slot1 0.93.0
==> resuming: checking slot1 holds this exact image
  slot1  6E90DCF1CB29B86677A4C5D28A3066B31CB42B4323A30C0A5A82C7EC62BBCB56
  local  F47DE115AA36FC97F8F1FA1E92E2670270231599D8C5EAE884E7C9099B4F26FF
```

Refused, exit 1, nothing marked. The old code would have downgraded the board
to `0.93.0` and reported success. **The board was already standing in the
failure case** - no plant was needed, only the willingness to run the check
against a state that was not the happy one.

A slot reports a plausible *version* while only part way written, because the
MCUboot header lands in the first kilobyte. Only the hash covers every byte,
which is why the same comparison now runs on the success path too: smpmgr
exiting 0 is not evidence about what is in flash either.

## The Ethernet link the panel could not report (2026-11-18, 0.98.0)

Reported from the bench: the RJ45 does not light with a cable in a live port.
Two separate defects, and the PHY was not one of them.

Read over the serial shell with `mdio read mdio 0 <reg>`, cable in a live
port, nothing written:

```
reg 0   BMCR     basic control                     0x0
reg 1   BMSR     basic status                      0x780d
reg 1   BMSR     second read                       0x780d
reg 2   PHY ID1  must be 0x0007                    0x7
reg 3   PHY ID2  must be 0xc131                    0xc131
reg 4   ANAR     what WE advertise                 0x1a1
reg 5   ANLPAR   what the PARTNER advertises       0x1
reg 17  MODECTRL mode control/status               0x0
reg 18  SPMODES  MODE strap + PHYAD                0x9b9d
reg 26  SYMERR   symbol error counter              0xffff
reg 27  SCSI     special control/status            0xffff
reg 29  ISRCFLAG interrupt source                  0xffff
reg 30  IMASK    interrupt mask                    0xffff
reg 31  PHYSCS   autoneg done, speed indication    0xffff
```

**BMSR 0x780d means the link is UP.** 0x7809 is this part's no-link default -
four capability bits, autoneg able, extended capability - and the one bit
added is `BIT(2)`, LINK. Autoneg complete, `BIT(5)`, is clear, which agrees
with BMCR 0x0000: autoneg disabled, 10 Mbit, half duplex. Forced 10BASE-T
half, exactly what the MODE 000 strap predicts, and it links.

### Trust in those reads, established rather than assumed

Five registers came back `0xffff`, which is what a failed MDIO read looks like
as readily as it is a value. The control is the PHY ID pair: it **must** read
`0x0007 / 0xc131` on a LAN8742A, and it does, twice, in the same sweep. So the
low registers are being read correctly and BMSR is evidence.

Registers 18 and 26 upward are not, and the pattern is by address rather than
random - 0 through 17 read plausibly and repeatably, 18 and 26-31 do not. A
marginal MDC would fail at random instead. **Unexplained, and not relied on
here**: every conclusion above rests on registers 0-5.

### Defect 1: the jack's lamps on the drawing were drawn once

`ethLedG` and `ethLedA` are server-rendered template variables, so the two
lamps on the drawn RJ45 were correct at the instant the page was rendered and
frozen from then on. Plug a cable into a page loaded without one and they stay
dark for as long as you watch.

**This is the third instance of one omission.** The DO and PDIO lamps had it
this morning; `boardLeds()` was written for exactly this and does not reach the
RJ45, because an RJ45 is not a channel and carries no `?ch=` link to find it
by. The lesson is not about either graphic: **anything the server draws from
state needs a line in the poll, and the poll has to be enumerated against the
drawing rather than extended whenever somebody notices.**

### Defect 2: a negotiated mode read off a negotiation that never ran

`eth_mdio_link()` derived speed and duplex from `anar & anlpar`. With autoneg
disabled no link code words are exchanged, so ANLPAR holds nothing - `0x0001`,
the selector field alone - and the AND names no mode at all. The panel showed
`speedMbit: 0` beside `link: true` and said "speed unnegotiated", which read
as a missing measurement and was a derivation asking a register nobody had
filled in.

Now BMSR's autoneg-complete bit picks the source: complete, and the common
advertisement is the answer; not complete, and BMCR is, because a forced mode
exists nowhere else. Published as `forced` so the two cannot be confused:

```
before   {"link":true,"fullDuplex":false,"speedMbit":0,...}
after    {"link":true,"fullDuplex":false,"speedMbit":10,"forced":true,...}
```

**Forced is not agreed, and that is the diagnosis rather than a label.** A port
that never negotiates links only where the partner tolerates one, so the same
cable in two switches gives two answers and neither is a fault in the cable.
Any future "Ethernet works here and not there" belongs against this line.

### What this does NOT change

The MAC is still dead - `rc -19`, `initRes -5`, `swr 1`, the DMA software reset
never completing for want of the RMII reference clock - and the strap
conclusion above stands untouched. Nothing here is progress on Ethernet
carrying traffic. What it fixes is a panel that could not report the state of
the wire, on a board where the wire is the only part of Ethernet that works.

## `whoami`, and the serial port that was already listening (2026-11-18)

Asked whether a `whoami` would need the serial port reconfigured to accept
commands. It does not: `CONFIG_SHELL=y` with `CONFIG_SHELL_BACKEND_SERIAL=y`
has been on since M0, and the port already carries `mdnsname`, `health`,
`mcuboot`, `pmc parts` and `pmca` beside Zephyr's own `kernel`, `net`, `i2c`,
`adc`, `gpio` and `mdio`. A command is one `SHELL_CMD_REGISTER`.

Measured by dropping the file from the build and rebuilding, rather than
estimated:

| | with | without | cost |
|---|---|---|---|
| FLASH | 1,080,208 | 1,079,428 | **780 B** |
| RAM | 436,592 | 436,592 | **0 B** |

A shell command is a table entry in a linker section plus its help and output
strings; none of it is RAM. The 83.27% RAM figure also settles an earlier
loose end: it is unchanged by `whoami`, and the 83.00% recorded when the
debug logging came off was simply before `pmc_pulse` and its 1 KB stack
existed.

It answers in one round trip what the wrong-board incident needed four
commands to establish, and deliberately prints **no address**: this output is
meant to be pasted verbatim into this file, and an address may not be
committed here. `net iface` is where those live.

## Three indicators that were decided once (2026-11-18, 0.100.1)

Reported from the bench: the RS485 lamps sit solid instead of blinking, and a
PDIO channel driven from outside looks identical to an idle one. Both are the
same defect as the RJ45 lamps above, found three more times in one pass, so the
pattern is now recorded as a rule rather than as three fixes.

**EVERY SERVER-RENDERED INDICATOR ON THIS PANEL NEEDS A LINE IN THE POLL.** The
page is one snapshot by design, which is right for a table of readings and
wrong for anything whose job is to show change. The lamps decided at render
time were: the terminal channel LEDs (fixed this morning), the RJ45's two
(fixed this afternoon), and now RS485 TX/RX. The enumeration is the fix - the
drawing was read element by element rather than waiting for the next report.

### RS485: a blink window belongs to the viewer, not the bus

`fillBusLamps()` compared the age of the last byte against a 3 s window in C
and emitted `act` or not. So a page loaded quiet showed a dead bus through any
amount of later traffic, and one loaded just after a byte blinked for as long
as it stayed open. Neither is activity; both are a stopped clock.

The firmware now publishes the **age in ms** and the page decides:

```
"rs485":{"txAge":-1,"rxAge":-1}
```

An age, not a boolean, because "recent" is a property of how fast a person can
see a lamp against a 2 s poll - a window shorter than the poll would blink only
when a byte happened to land in the gap. Driven through the page's own poll
with the ages forced, nothing transmitted:

```
ages -1          tx "led red"       rx "led green"
txAge 120        tx "led red act"   rx "led green"
both recent      tx "led red act"   rx "led green act"
both 9 s stale   tx "led red"       rx "led green"
```

Also **red TX, green RX**, which is what the lamps on the carrier do. They were
amber-over-green, a convention from nowhere; an indicator whose color does not
match the one beside it on the hardware makes a person check the drawing
against the board instead of trusting it.

### `data-tpl-attr` IS CONSUMED, and the selector that used it matched nothing

The first version of the RS485 fix selected the lamps by
`[data-tpl-attr~="tx485Cls"]`. The template engine **acts on that attribute and
does not emit it**, so the rendered circle carries no trace of which variable
filled its class. `querySelector` returned null, the lamp never updated, and
**it threw no error** - a dead lamp looks exactly like a quiet bus.

Caught by reading the rendered DOM rather than the source:

```
{"cls":"led red","cy":"282","tpl":null}     <- tpl null, not "class tx485Cls"
```

The lamps now carry `id`s. The general form, which is this file's recurring
shape in a new costume: **a template directive is not a hook.** The source
attribute and the served attribute are different sets, and only one of them is
what a script can find.

### The PDIO read-back row, because command and level are two facts

The connector's PDIO lamps follow the **command**, which is what an output
indicator must do and was the right fix this morning. It left a channel driven
high from outside - 24 V on the terminal, nothing commanded - looking exactly
like an idle one, because the drawing showed no read-back anywhere.

So twelve lamps now sit **below** the PROGRAMMABLE DIGITAL I/O block: above the
connector is what the board was told, below it is what the line reads. Measured
with nothing commanded this boot:

```
pdioCmd  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
pdio      0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0
row lit  [2]        unknown []
```

**A lit lamp there means "this line is high", not "an input is high".** A
channel commanded high reads high through its own output driver, so the row
cannot be labeled "input" without being wrong half the time.

It is driven only from the poll and never server-rendered - deliberately, since
a lamp with no rendered state cannot be frozen at page load, which is the
defect the row exists to answer. And `pdioOk` false marks the whole row
`unknown` (hollow) rather than dark: a port that did not answer is not a port
reading low.

### UNRESOLVED: 24 V reported on PDIO4, read back on index 2

The bench report is that **PDIO4** is wired to 24 V. With nothing commanded
this boot the board reads index **2** high and every other index low. Two
candidates, and this firmware can separate neither:

- the wire is on the terminal marked **02**, not **04**; or
- the PDIO **read** bit map is wrong - `r:P23` for channel 04 against `r:P25`
  for channel 02.

Neither is confirmed. The second is risk 5 of this project's plan, and the
whole reason every PDIO row in `tools/terminals.csv` still says `pinout`: the
write map, the read map and the DIN map are each scrambled differently, all
three were transcribed from upstream `main`, and not one has been confirmed at
a connector.

**One wire move settles it.** Put the 24 V lead on the terminal marked **00**
with nothing commanded: index 0 lighting means the map is right and the lead
was simply on 02; index 11 or anything else lighting means the read map is
wrong, and which index lights names the error. Do it one terminal at a time and
the `conf` column for all twelve can finally read `measured`.

Noted beside it, because it was in the same capture and may not be expected:
`din` reads **DIN2 high** as well. Not a crossed read - before the reset, PDIO
channels commanded high showed as 1 in `pdio` while `din` stayed all low, so
the two arrays are independently sourced.

### The log that lied by omission

`note()` was `say.textContent = text`: one line for the whole panel, so every
reply erased the one before it. Reading several channels in a row left only the
last answer on screen - and because a channel reading HIGH is the one worth
announcing, what survived looked like "only this channel answered" when all of
them had. A single-line log is a log that lies by omission.

Each command now takes its own line when it is sent and its reply lands on that
line. Proved with commands the board **refuses**, so nothing was energized -
the token was cleared first and restored after:

```
refused: bad or missing token (3 refusals since boot)     <- newest, bright
refused: bad or missing token (4 refusals since boot)     <- answered LAST
refused: bad or missing token (2 refusals since boot)
refused: bad or missing token (1 refusals since boot)
token forgotten
```

The second line is the proof: that command's reply arrived **after** the one
above it and still landed in the position where it was issued. Replies cannot
overwrite each other and cannot swap places. Four clicks, four refusals,
counted 1-4 by the board itself.

**And a check that was wrong, not a defect.** `doOut` read `0,1,0,0,0,0,0,0`
right after that test, which looked like a refused command having energized an
output. It had not: `status_out.c` drives **DO1** as the health heartbeat, and
the sample caught the blink high. Six samples at 700 ms afterwards read all
zero. Worth keeping because "an output I did not command is on" is the right
thing to be alarmed by, and the answer is that one channel has an owner.

## What actually guards a write (2026-11-18, 0.101.0)

Asked whether this description is true: *anyone on the network can be served
the state of the board; setting anything needs a magic number entered in the
browser, compared against a key on the device; decompiling the board will not
recover the key.*

The first two are right. **The third is wrong**, and it is the one that
matters. Measured rather than reasoned, against the running board and the
signed image.

### Reads are open. Confirmed.

No token, no credentials, nothing:

```
GET /state.json    200, 1835 bytes of board state
GET /rs485.json    200, 284 bytes
```

Every GET route is `BIT(HTTP_GET)` with no gate. That is the design - the panel
is a diagnostic - but it means the readings, the slot versions, the health
verdicts and the RS485 capture window are public to anything that can reach the
port.

### Writes need the token. Confirmed.

```
POST /io  do=set&chan=DO0&v=1   (no token)
  -> refused: bad or missing token (1 refusals since boot)
```

Refused before anything is touched, and counted where the panel can show it.

### The token is NOT safe from the image

`auth.c` holds it as `static const char tok[] = SECRET_API_TOKEN` and compares
it byte for byte. It is not hashed. A byte search of the signed image finds it:

```
token length      : 32 ASCII bytes
image             : 1093930 bytes (signed)
plaintext matches : 1
byte offsets      : 0xF150A
```

**One `strings` run recovers it.** Anyone holding a firmware image - which is
every image ever flashed, and every signed binary that has crossed this
network during an OTA - has the write token.

Signing does not help: MCUboot signatures prove an image's ORIGIN, they do not
conceal its CONTENTS. The image is not encrypted, and `zephyr.signed.bin` is
the plain binary plus a header and a signature.

Hashing it would close this half and not the other: **the token crosses the LAN
in cleartext in every POST body**, because this is plain HTTP. Anyone who can
read the traffic has it regardless of how it is stored. `auth.h` has always
said that part.

### CSRF is documented and not implemented

`auth.h` claimed "the token here authorizes, HtmlTplForm's token proves the
request came from a page this board rendered - a write needs both". The engine
does carry a CSRF token and the page does render one. **Nothing in the write
path checks it.** A grep for `csrf` across `app/src` finds two comments and no
code.

Proved without touching an output, by sending a bogus verb so the only thing
being tested is which gate answers first:

```
POST /io  token=<correct>&do=notaverb      no CSRF field, no Referer
  -> refused: unknown verb 'notaverb'
```

It reached the verb dispatcher. The token alone is the whole gate.

**A doc comment claiming a control that does not exist is worse than no
comment**, because it is what gets quoted when somebody asks what guards the
board - which is exactly how this question arose. The comment now says what the
code does.

### The accurate sentence

Anyone on the network can read everything. Writing needs a 32-character token
entered in the browser, compared in constant time against one compiled into the
firmware - which stops a stray request, a scanner and an unauthorized script,
and stops nobody who has an image of this firmware or who can watch the
traffic. There is no second factor. **The control is the network, and the token
is a latch on the gate, not a lock.**

## Settings that survive a power cycle (2026-11-18, 0.103.0)

Asked for: digital outputs, strobes, probe type and RS485 settings remembered
from one power cycle to the next. Two decisions were taken before any code,
because the obvious implementation collides with a safety interlock.

### The interlock stays, and the restore waits for the health gate

docs/SAFETY.md's interlock (0) drives every digital output de-energized in the
earliest init, before networking, so that a reboot or an OTA revert cannot
leave 24 V in whatever state another image set. Restoring remembered outputs at
boot would simply delete that.

The resolution chosen was to keep the interlock and restore **after the health
gate passes** - all five checks, which means at least 60 s of uptime, both
expanders answering, the analog switches open and the panel having served a
real HTTP 200. So an image that crash-loops, or one about to be reverted
because it never got healthy, never re-energizes anything.

Measured end to end. Two outputs set, the board reset, polled throughout:

```
 24s  up  31s  doOut 00000000  failing: uptime,panel
 33s  up  40s  doOut 00000000  failing: uptime
 49s  up  56s  doOut 00000000  failing: uptime
 58s  up  65s  doOut 00000000  failing: none        <- gate passes
 64s  up  68s  doOut 00100001  failing: none        <- restored
```

Sixty seconds de-energized, then back. The order is: everything low, prove it,
then put back what was set.

The off state is remembered too, which is not the same claim: a second cycle
with both channels switched off came back `00000000` and stayed there, so a
stored record is honored rather than a stored record being a list of things to
switch on.

RS485 and probe type restore during init instead, because neither energizes
anything. Proved by changing them away from their defaults first - a setting
that kept its default across a reboot proves nothing:

```
  baud       : 9600   (changed from 19200 before the reset)
  probe type : K      (changed from PT100)
```

### The defect: the feature erased its own memory

First version stored DO7 on, reset, and came back with the record reading
`00000000`. Not a failed write - a successful one, of the wrong thing.

`pmc_persist_init()` applies the stored configuration **through the same
setters a person uses**, which is the right call: a stored value this image no
longer accepts must be refused exactly as a typed one would be. But every one
of those setters calls `pmc_persist_note()`. So restoring the RS485 line
settings scheduled a save for three seconds later - while interlock (0) still
had every output low and the health gate was fifty seconds away. The snapshot
wrote all-off over the record it was about to restore from.

**Before the restore, the live outputs are not a statement of anyone's intent -
they are the interlock's starting position.** Saving them is saving the absence
of the thing being remembered. `save_work()` now refuses to write the output
record until the restore has happened, and `pmc_persist_restore_outputs()`
notes once on its way out so the post-restore state is captured.

It would have shipped looking like it worked: the panel showed the right thing,
the record existed, `records written` counted up. Only a power cycle tells you,
which is the one test a persistence feature cannot skip.

### Smaller things the same test found

- **`have_cfg` / `have_out` were load-time flags only**, so immediately after a
  save the `persist` command said "none stored" while reporting `records
  written: 2`. Two statements about the same store disagreeing in one screen.
  Set on a successful write now.
- **Records are versioned and rejected whole.** An OTA can change these structs
  while NVS holds the previous image's bytes, and a struct read back at the
  wrong size would energize outputs nobody chose. Both are `__packed`, carry a
  version byte, and are rejected on either a length or a version mismatch -
  falling back to "nothing remembered", which is the safe direction.
- **The save is deferred 3 s and coalesced**, and skipped entirely when the
  snapshot matches what is stored. Dragging a row of outputs on and off costs
  one flash record, and putting a control back where it started costs none.

## The DO1 boot indicator, removed (2026-11-18, 0.103.0)

It drove digital output 01: steady on while running but not yet healthy,
blinking once every check passed, dark when the firmware was not running at
all. It is gone, and the reason is the feature above - an indicator the
firmware strobes cannot also be a channel a person sets and expects to find
where they left it. DO1 would have been the one output that silently refused to
keep its state. Two owners, one 24 V high-side switch.

Verified by sampling: ten reads over seven seconds, `00000000` every time,
where the same sample used to catch the blink high about one time in sixteen.

**What was given up, recorded so nobody has to rediscover it.** After a reset
there is now nothing on this board that distinguishes "running but not yet
healthy" from "in the DFU bootloader" from "crashed before POST_KERNEL" from
"unpowered", unless the network is up and the panel can be reached. That
distinction was the whole point of a blink rather than a level: a bootloader
cannot fake a heartbeat. If it is ever wanted back it is a `k_work_delayable`
toggling one channel - and the channel it takes must come out of the remembered
set.

`status_out.c` is now `do_interlock.c`, because a file named for a status
output that no longer drives one is the kind of name that sends the next reader
looking for a feature that was deleted. What remains in it is interlock (0) -
which must not go - and the `dowalk` command, kept because lighting each output
in turn is the measurement that retires the [LIB] DO channel map, and it
deliberately bypasses the remembered set so a bring-up sweep is not a state
anybody has to find restored at the next power-up.

## Ethernet works, and the conclusion above was wrong (2026-11-18, 0.107.0)

The RJ45 carries traffic. `initRes 0`, carrier up, 100 Mbit full duplex,
auto-negotiated, a DHCP lease, and the panel served over it:

```
eth: {"present":true,"rc":0,"carrier":true,"link":true,"fullDuplex":true,
      "speedMbit":100,"forced":false,"hasAddr":true,"initRes":0}

Ethernet link speed: 100 Mbits full-duplex
DHCPv4 state      : bound

full panel: 167393 bytes in 157 ms  = 1,041 kB/s
```

**1,041 kB/s over the wire against about 85 kB/s for the same payload over the
radio** - better than ten to one, on the link that was reported as broken
hardware.

### The fix

Ten lines, in `pmc_eth_phy_relatch()`: pulse PJ15 - the PHY's nRST - once more
at init priority 78, after the regulator that releases it at 75 and before the
MAC initializes at 80.

That is the whole change. The LAN8742A samples its configuration straps on
**every rising edge of nRST**, not only at power-on, and nRST is a GPIO this
firmware already owns.

### What was actually wrong

At boot the PHY latched its straps wrong, two ways at once:

| strap | latched | should be | consequence |
|---|---|---|---|
| MODE[2:0] | 000 | 111 | 10BASE-T half, auto-negotiation off |
| nINTSEL | nINT | REFCLKO | **pin 14 idles high instead of sourcing 50 MHz** |

The second one is what killed it. The MAC's DMA software reset is clocked by
the RMII reference clock; with no clock the reset never completes,
`HAL_ETH_Init()` returns -EIO after its 500 ms timeout, and Zephyr reports no
Ethernet at all.

### The three measurements that found it, in order

**1. Arduino's own schematic says the PHY must source the clock.** Net
`ET0RCLK` runs from the MCU to U5 pin 14, and the symbol names pin 14
`nINT/REFCLKO`. Corroborated by three other pins whose numbers the symbol also
gives: `ET0DV` to pin 11 (`CRS_DV/MODE2`), `ET0TX1` to 18 (`TXD1`), `ET0MDC`
to 13. So the design REQUIRES REF_CLK Out mode - which means a module that
cannot drive that clock would be a module on which Ethernet never worked for
anybody, and Arduino ships and documents it.

Also settled there: `ET0RCLK` lands on ball **N4, which is PA1**, while
**PA1_C is ball T2**. The two are joined only by the SYSCFG analog switch, so
the switch is NOT in the Ethernet clock path and opening it for AI2 costs
Ethernet nothing. The Kconfig help's "genuine either/or" worry is answered: it
is not one.

**2. `ethreset` - retry the DMA reset at runtime.** This separates "no clock"
from "a clock that arrives too late", which the boot-time failure cannot. Run
80 seconds after boot with the PHY long up and answering MDIO:

```
before:  DMAMR 0x00000001  SWR 1
after:   SWR STILL SET after 501 ms
```

Not a race. No clock, even at steady state.

**3. Sample PA1 with the CPU - no scope needed.** `GPIOA->IDR` reflects the
pad even in alternate-function mode, so a read loop is a crude clock detector.
It cannot measure a frequency and does not need to; the only question is
whether both levels appear.

```
PA1 MODER 2 (alternate function), AF11 (ETH - correct)
PA1 sampled 1000x: 1000 high, 0 low  -> STATIC
```

Our pinctrl was right, and the pin was dead - held high, which is what an
open-drain interrupt line looks like idle. That is pin 14 behaving as nINT.

**Then the same three, after pulsing nRST:**

```
BMCR after re-reset: 0x3000 (CHANGED)          <- was 0x0000
PA1 sampled 1000x: 996 high, 4 low  -> TOGGLING
before:  DMAMR 0x00000000  SWR 0
after:   SWR CLEARED in 0 ms
<inf> phy_mii: PHY (0) Link speed 100 Mb, full duplex
```

### WHY the boot-time latch is wrong is still not known

Three candidates, and no measurement separating them:

- the MCU's Ethernet pads are not yet in alternate-function mode when the
  regulator releases reset at priority 75 - the MAC configures them at 80;
- a supply rail or the PHY's 25 MHz crystal is not settled that early;
- the regulator's `startup-delay-us = <50000>` is not being honored the way it
  reads.

**The fix works whichever it is, and that is why it is a second pulse rather
than a reordering.** But it is a workaround over an unexplained cause. If the
first candidate is right the honest fix is upstream, in the board's init
order, and worth sending there.

### The error that cost the detour

The earlier conclusion named a real mechanism - the straps were genuinely
wrong, and nINTSEL genuinely was selecting nINT - and then asserted two things
that were not measured: that the strap state came from the carrier's LED
circuit, and that "the strap is the only control for REFCLKO, so the clock is
not fixable in firmware". The second is false for a reason stated in the same
datasheet: straps re-latch on nRST, and nRST is a pin this firmware drives.

The two caveats recorded beside that conclusion were both right, and both
named the test that would have settled it a day earlier: scope PA1, or boot
the mbed example and see whether Ethernet works there. The first was never
done because it needed a scope - and the CPU could have done it, badly but
well enough, the whole time.

### What this does not change

WiFi is still the default interface. Ethernet being up and faster is an
argument for revisiting that, not a reason to have already done it.

## Ethernet preferred, radio as fallback, decided every 2 s (2026-11-18, 0.108.0)

`CONFIG_NET_DEFAULT_IF_ETHERNET=y` only sets the default at boot, before any
link exists, and a compile-time setting cannot know whether a cable is plugged
in. A default interface that is DOWN is worse than the slower one that is up -
an outbound connection binds through the default, so DNS and NTP would fail
with a perfectly good radio sitting beside them.

So `pmc_net_pick_default()` re-makes the choice on every link poll:

    Ethernet, if it is up AND has an IPv4 address
    otherwise WiFi, if it is up AND has an IPv4 address
    otherwise leave whatever is set alone

**It keeps no memory of what it chose last time**, so there is no state to get
stuck in and the same three lines handle every transition in both directions.

Measured, taking interfaces down from the shell - not the same as pulling the
cable, but it fails the same `net_if_is_up()` test the selector uses:

```
start                 default = 1   (eth0)
after eth0 down       default = 2   (wlan0)
after eth0 up         default = 1   (eth0)
after wlan0 down      default = 1   (eth0)
after wlan0 up        default = 1   (eth0)
```

Down and back, both ways. The fourth line is the one worth reading: the radio
dropping while the wire is up changes nothing, because there is nothing better
to fall back to.

**UP, NOT CARRIER, AND AN ADDRESS AS WELL.** `net_if_is_up()` is admin state
and carrier together. And an interface can hold carrier with no address for
the whole of a DHCP exchange - several seconds on a cable newly plugged in -
and making it the default in that window sends traffic out of an interface
with no source address to put on it.

**This is a fallback, not a failover.** Changing the default does not move
existing connections. The HTTP server binds INADDR_ANY and keeps serving on
both interfaces regardless; what the default steers is new outbound traffic
and source-address selection. Unplugging the cable drops the sessions that
were on it.

### Narrowing why the boot-time strap latch is wrong

One of the three candidates recorded in the previous section is now
**eliminated**, without a flash cycle, by reading what the driver does.

`pinctrl_apply_state()` is called inside `eth_initialize()` - that is
`eth_stm32_hal_common.c:107`, and `eth_initialize` runs at
`CONFIG_ETH_INIT_PRIORITY=80`. `pmc_eth_phy_relatch()` runs at **78**. So the
successful second pulse happens with the MCU's Ethernet pads in exactly the
state they were in at priority 75, when the regulator's release latched the
straps wrong: not yet in alternate-function mode, either time.

**Pad state cannot be the difference, because it is the same on both edges.**
What differs is elapsed time and the explicit 25 ms assert plus 100 ms settle.
So the cause is settling - a rail, the PHY's 25 MHz crystal, or the
regulator's `startup-delay-us = <50000>` not being honored the way it reads -
and not init ordering.

That also means the fix is unlikely to belong upstream as a reordering, which
is what the previous section guessed at. The remaining experiment is to vary
the delay: if lengthening the regulator's startup delay alone makes the first
latch correct, it is settling time and the number is measurable.

## The PHY keeps its straps across a soft reset, and that faked a hardware fault (2026-11-18, 0.109.0)

**CORRECTION to the section above.** "Ethernet works from boot, fix verified"
was wrong about the second half. The fix had never been observed to do
anything.

`RST0ETH` carries **R5, the MCU's PJ15, and the PHY's nRST (pin 15)**, and R5's
other end is on the 3V3 net - it is a **pull-up**. So when the MCU resets, PJ15
goes high-impedance and R5 holds the PHY's reset released. **An MCU soft reset
never resets the PHY.** It keeps whatever it latched at power-on.

Every reset available from here - OTA, `POST /reset`, `sys_reboot` - is a soft
reset. So:

- one bad latch at some power-on explains Ethernet failing *consistently*
  across dozens of reboots, which is what made it look like broken hardware;
- `ethreset phy` fixed the latch by hand, and every soft reset since preserved
  the GOOD latch;
- which means `pmc_eth_phy_relatch()` has only ever run against an
  already-correct PHY. It was never the thing that fixed anything.

### The control, run properly, on a real power cycle

`CONFIG_PMC_ETH_PHY_RELATCH=n` - a Kconfig that exists only to reproduce the
original fault - then power removed and restored:

```
fw 0.109.0  up 199s
eth: {"rc":0,"carrier":true,"link":true,"fullDuplex":true,
      "speedMbit":100,"forced":false,"hasAddr":true,"initRes":0}

mdio read mdio 0 0   ->  0x3100
```

**Ethernet came up with the fix compiled out.** `BMCR 0x3100` is the
LAN8742A's textbook reset default - auto-negotiation enabled, 100 Mbit, full
duplex, MODE strap 111. **The power-on latch is correct.**

The latch value also CHANGED across the power cycle, 0x3000 to 0x3100, which
is what proves the PHY genuinely cold-started - worth checking, because the
jack's lamps stay lit with the board unpowered, so the link partner is feeding
something back through the magnetics.

### So what was actually broken, and what the fix is for

The best-supported account: an early build had `CONFIG_ETH_INIT_PRIORITY=60`
against a PHY reset regulator at 75 - the ordering bug already recorded in
docs/CONFIG.md. That could latch the straps wrong, and once latched the
pull-up carried the bad state through every subsequent soft reset **including
every reset after the ordering was fixed**. The ordering fix looked like it
had not worked, because nothing had re-latched the PHY since.

Not proven - proving it means deliberately re-creating a bad latch, and there
is no known way to do that from software. Stated as the account that fits
every observation.

`pmc_eth_phy_relatch()` is KEPT, as insurance rather than as a fix: it costs
125 ms of boot and it makes a board that somehow lands in a bad latch heal
itself on the next reset, which is exactly the failure that cost this project
days. `CONFIG_PMC_ETH_PHY_RELATCH=n` reproduces the original conditions.

**The lesson is about the test, not the PHY.** A reset that does not reset the
thing under test is not a test. Every "it still fails after a reboot" in this
investigation was evidence about one latch, re-read many times.

## Reduced motion must not remove the information (2026-11-18, 0.111.0)

Reported from the bench: the RJ45 on the panel was not showing solid green and
blinking amber. This machine has `prefers-reduced-motion: reduce` set, and the
rule was:

```css
@media (prefers-reduced-motion: reduce) { .leda.act { animation:none; } }
```

`animation:none` parks the lamp on whatever fill it had. "There is traffic on
this link" became a lamp that never moves - **indistinguishable from an idle
one**, which is the single thing the indicator exists to show. The accessibility
preference did not reduce the motion, it deleted the message.

Now a 2 s ease fade instead of a 1.3 s step blink: no fast flashing, still
plainly alive. Verified by sampling the computed fill 24 times over two
seconds - `rgb(245,195,92)` lit and `rgb(58,38,8)` dark, both present.

**Second time this exact rule bit.** The RS485 bus lamps had it earlier the
same day, where `animation:none` left them SOLID LIT - which read as
"continuous traffic" rather than as "no animation". The general form:
suppressing an animation must preserve what the animation was saying. If the
only difference between two states is motion, removing motion removes the
state.

The lamps were also lit too dimly to read - `#1f8a3c` and `#b4700f` with no
glow, against `#38d16a` and `#f5c35c` with a drop-shadow everywhere else on
the same drawing. They now use the panel's own lit colors, so "on" means one
thing across the whole graphic.

## The token field could not be read back on Chrome (2026-11-18, 0.111.0)

`<input type="password">` gets a reveal button from Edge and **none from
Chrome**. So the write token could be proof-read on one browser and was a row
of dots on the other - no way to check a paste, and one wrong character
reports only `refused: bad or missing token`, which says nothing about which
character.

A `show` / `hide` button now sits beside it, toggling only the input's `type`.
`forget` returns it to hidden, because an empty field left on "show" is a field
that will reveal the next token typed into it.

## CAN and the encoders (2026-11-18, 0.115.0)

### Neither encoder can use the hardware quadrature decoder

Checked against `stm32h747xihx-pinctrl.dtsi` before a line of driver code, the
same way AO2 was settled. STM32 encoder mode needs channels 1 and 2 of ONE
timer on A and B:

| channel | A | B | I |
|---|---|---|---|
| ENC0 | PJ8 = `tim8_ch1` | PH12 = `tim5_ch3` | PH11 |
| ENC1 | PC13 = **analog only** | PI7 = `tim8_ch3` | PJ10 |

ENC0's A and B are on different timers and B is CH3 rather than CH2. ENC1's A
carries **no alternate function at all** on this package - PC13 is a
backup-domain pin and can only ever be a GPIO here.

So both are decoded in software from GPIO interrupts at 4x. That has a ceiling
software decoding has and a timer does not: roughly 200 k edges per second
across both channels, which a 1000-line encoder reaches at about 3000 rpm.
**Past it counts are lost silently** - the state machine still sees legal
transitions, just fewer of them. The error counter catches an ILLEGAL
transition, which is a different failure, so a clean error count is not
evidence of a correct count. The honest check is the distance between index
pulses against the encoder's line count, which is why that is measured and
reported rather than used to silently rewrite the position.

### CAN: TX and RX are the other way round from the mbed library

`pins_mc.h` records `CAN TX PB_8 RX PH_13`. This package offers only
`fdcan1_rx` on PB8 and only `fdcan1_tx` on PH13, and no second FDCAN function
on either pin. The silicon's own table wins over another program's header;
`tools/terminals.csv` has been corrected and the overlay wires it that way.

Still [MBED]-grade and unconfirmed at the connector: nothing has yet put a
frame on a wire and watched it arrive.

STB is on **PA13, which is also SWDIO**, so bringing CAN up ends any chance of
attaching a debugger until the next reset. This carrier brings out no debug
header, so nothing is lost here - and DO7 already sits on PA14, SWCLK, so the
pair was half gone already. Its polarity is a guess and is labeled as one: the
control reports the LEVEL it drives rather than claiming what that level means.

### Proved with loopback, because one node cannot test a bus

A CAN controller alone on a bus cannot complete a transmission - with nobody
to acknowledge, it retries and then counts an error. That is the bus answering
honestly, and it is counted separately from a real failure so the panel can
say "nothing acknowledged this" rather than "the controller is broken".

Internal loopback needs no wire and no second node:

```
frames   tx 2 ok, 0 unacknowledged, 0 failed; rx 2
last 2 received, newest first:
    129139 ms   0x7ff       2  01 02
    126595 ms   0x123       4  de ad be ef
```

Identifiers, lengths and payloads all returned intact, so the driver, the
accept-everything filter, the DLC conversion and the receive ring are all
proved. **What is NOT proved is anything past the MCU pins**: the transceiver,
the STB polarity, the termination and the connector are all untested, and need
a second node on the wire.

## A gpio_callback is a list node, and sharing one broke the WiFi radio

The worst bug of the session, and the symptom pointed nowhere near the cause.

Adding the encoders made the radio fail **every** SDIO IOCTL:

```
Error - failed to allocate a packet buffer for IOCTL
Buffer alloc failed in function whd_wifi_get_ap_info at line 3334
Connection request failed (-11)
```

It looked like resource exhaustion and was not. Measured first, which ruled
the obvious answer out: `kernel heap` showed **117,240 bytes free**, and
`net mem` showed every system pool fully available, RX 64/64 and TX DATA
320/320. Nothing was exhausted.

The radio has its **own private pool** - `NET_BUF_POOL_FIXED_DEFINE(airoc_pool,
20, 2400, ...)` in `airoc_wifi.c`, which is why it appears in neither of those
reports. Twenty buffers, all held from boot by IOCTLs that never completed.

### The cause

`struct gpio_callback` **begins with a `sys_snode_t`**: it IS the list node,
not a thing that holds one. The first version of enc.c registered ONE callback
struct on TWO ports, because every channel here straddles two - ENC0 is gpioj
and gpioh, ENC1 is gpioc and gpioi:

```c
gpio_init_callback(&s->cb_ab, ab_isr, BIT(h->a.pin) | BIT(h->b.pin));
rc = gpio_add_callback(h->a.port, &s->cb_ab);
if (rc == 0 && h->b.port != h->a.port) {
        rc = gpio_add_callback(h->b.port, &s->cb_ab);   /* same node, twice */
}
```

That links one node into two lists and corrupts both. ENC0's A line is **PJ8**,
so the shared node went into **port J's** callback list - where the AIROC WiFi
driver keeps its host-wake callback on **PJ5**. The radio's interrupt path was
destroyed, its transactions stopped completing, its buffers were never
released, and every later call failed to allocate one.

**The encoders kept counting correctly throughout**, which is why they looked
innocent.

Fixed with one callback struct per pin, each registered on its own port with a
single-pin mask. The mask matters on its own: `BIT(8) | BIT(12)` registered on
port J would also fire the handler for PJ12.

### How it was found, which is the part worth keeping

Not by reading. By a Kconfig that turns each half off:

```
CONFIG_PMC_ENCODER_CHANNELS=0, CAN on   ->  radio moved 1967 packets
CONFIG_PMC_ENCODER_CHANNELS=1, CAN on   ->  radio dead
CONFIG_PMC_ENCODER_CHANNELS=2, fixed    ->  radio moved 2841 packets
```

Two flash cycles took it from "two new subsystems and a dead radio" to "ENC0
alone does it", which pointed at PJ8 and made the code defect obvious on
re-reading. **The bisection switch was worth more than any amount of staring
at the diff**, and it is kept in the tree for the next time.

It also says something about where the suspicion went first: PC13 was the
prime suspect throughout - a backup-domain pin with no alternate function,
sitting on the same port as SDMMC1's PC8..PC12 - and it was innocent. The
measurement disagreed with the theory on the first try.

### Still untested

- **The encoders have never been turned.** Both channels are armed and read
  their lines; no shaft has moved, so no count, no direction and no index
  pulse has ever been observed. ENC0 counting plus or minus by hand, and
  index-to-index equalling 4x the line count, is the test that retires this.
- **CAN has never reached a wire.** Loopback proves everything up to the MCU
  pin and nothing beyond it.
