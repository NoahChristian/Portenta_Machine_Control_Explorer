# Read from the manual

What the Arduino Portenta Machine Control datasheet (AKX00032) and the Portenta
H7 schematic say. **Nothing on this page has been seen on hardware.**

Section numbers are written hyphenated — `3-2`, not `3.2`. Four dot-separated
numbers read as an IP address to the secrets gate, and a reference that blocks
its own commit is a reference nobody writes twice.

One of three pages, split by kind of confidence. A fact has exactly one home;
moving it up the ladder means **deleting** it from the page below, not copying
it.

| Page | Status |
|---|---|
| HARDWARE_FROM_MANUAL.md (this page) | read from a datasheet; never observed |
| [INFERRED_FROM_MBED.md](INFERRED_FROM_MBED.md) | what another program believes |
| [MEASURED.md](MEASURED.md) | observed here |

---

## The board, section 3-2

Topology table, in full, because the J-numbers are referenced everywhere else:

| Ref | What | Ref | What |
|---|---|---|---|
| J3 | Digital inputs | J10 | Encoders |
| J4 | Power supply 24 V | J11 | Analog out |
| J5 | HMI — RS-232, RS-422, RS-485, CAN | J13 | Ethernet |
| J6 | Digital outputs | J14 | WiFi / BLE SMA |
| J7 | Temperature probes | J15 | USB A |
| J8 | Digital programmable | J16 | USB micro |
| J9 | Analog in | J17 | Grove I2C |
| PB1 | **RESET push button** | U10 | MAX31855KASA+T thermocouple converter |

**PB1 is the only button on the board.** A search of the whole datasheet for
`swd`, `jtag`, `debug`, `boot0` and `boot sel` returns nothing but matches on
"resettable fuse". There is no debug header. This is the single fact that
shapes the install path — see [MEASURED.md](MEASURED.md).

## Compute and memory

- STM32H747XI, Cortex-M7 up to 480 MHz plus Cortex-M4 up to 240 MHz
- 2 MB flash with read-while-write, 1 MB RAM
- 8 MB SDRAM, 16 MB QSPI flash
- **NXP SE0502 secure element** — deliberately unused; see the README for why

## Digital inputs, section 3-3

8 channels on J3. Each is a 680 kΩ / 100 kΩ divider, so a 0-24 V input is
scaled to 0-3 V.

## Digital outputs, section 3-4

8 channels on J6, as **2x TPS4H160AQPWPRQ1** high-side switches — four channels
per part. Current limit and inductive kick-back protection. 0-500 mA per
channel.

J6 pin order, which matters because the silkscreen counts from **00**:

| Pin | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|---|---|
| | 24 V IN | 00 | 01 | 02 | 03 | 04 | 05 | 06 | 07 | GND |

So channel `01` is J6 pin 3. The 24 V on pin 1 is non-galvanically isolated
with respect to the board input voltage, and **nothing on an output is visible
without it**.

## Programmable digital I/O, section 3-5

12 channels on J8, each a high-side switch with current limit and kick-back
protection, **each also usable as a digital input** — same 680 kΩ / 100 kΩ
divider as J3.

Section 3-5-1 records that after a current-limit event the channel "must be
toggled to activate the channel again", which is the latch behavior that makes
driving an output a two-step operation rather than a pin write.

## Analog input, section 3-6

3 channels on J9, each software-configurable:

- **0-10 V** — 100 kΩ / 39 kΩ divider, scaling to 0-2.8 V
- **4-20 mA**
- **NTC / PTC**

The datasheet does not say *how* the mode is selected. That answer comes from
the mbed library — see [INFERRED_FROM_MBED.md](INFERRED_FROM_MBED.md) — and it
is four GPIOs per channel, not a register.

## Analog output, section 3-7

4 channels on J11, DC voltage, configurable 0-10 V, max 20 mA per channel.

## Temperature, sections 3-8 to 3-12

3 channels on J7, each software-selectable between:

- thermocouple **K or J** — MAX31855KASA+T (U10 in the topology table)
- **PT100 RTD** — MAX31865ATP+T, 2-wire and 3-wire both documented

## Other

- 2 encoders on J10, A/B/I
- CAN and RS-232/422/485 on J5
- Ethernet on J13, RJ45 with onboard transformer
- WiFi and BLE through the Portenta H7, antenna on the **SMA** connector J14 —
  2.4 GHz 802.11 b/g/n, BLE 4.2 by firmware and 5.1 by hardware
- Programmable channels are pulled up to the board 24 V supply through 10 kΩ

## Open questions this page cannot answer

- Which I2C bus the expanders sit on. Answered by measurement, not here.
- Which pin drives each channel. The datasheet names no MCU pins at all.
- The 0x36 device found on the Portenta's internal I2C bus.
