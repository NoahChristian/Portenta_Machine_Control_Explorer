# Inferred from the mbed library

What `Arduino_PortentaMachineControl` believes about this hardware.

**This is what another program believes, not what the silicon says.** The
library is mbed-only (`architectures=mbed, mbed_portenta`), it is not
authoritative about the board, and it has its own bugs like anything else. Read
every line here as a hypothesis with a good pedigree.

**Provenance.** The library is *not installed on this machine* — absent from
both `Documents/Arduino/libraries/` and `AppData/Local/Arduino15/libraries/`.
Everything below was read from the upstream repository's `main` branch, not
from a `v1.0.5` checkout. **Pin the commit when any of this is transcribed into
code**, because a pin table is version-specific and `main` moves.

One of three pages, split by kind of confidence. A fact has exactly one home;
moving it up the ladder means **deleting** it from the page below.

| Page | Status |
|---|---|
| [HARDWARE_FROM_MANUAL.md](HARDWARE_FROM_MANUAL.md) | read from a datasheet |
| INFERRED_FROM_MBED.md (this page) | what another program believes |
| [MEASURED.md](MEASURED.md) | observed here |

---

## MCU pins, from `src/pins_mc.h`

### Analog inputs — and the mode is not a register

```
AI0  PC_3C      AI1  PC_2C      AI2  PA_1C
```

Each channel has **four** mode-select GPIOs. That is the answer the datasheet
does not give: 0-10 V / 4-20 mA / NTC is a 4-bit switch network, not a write to
a mode register.

| Channel | IN1 | IN2 | IN3 | IN4 |
|---|---|---|---|---|
| AI0 | `PD_4` | `PD_5` | `PE_3` | `PG_3` |
| AI1 | `PD_7` | `PH_6` | `PJ_7` | `PH_15` |
| AI2 | `PH_10` | `PA_4` | `PA_8` | `PC_6` |

Which 4-bit code means which mode is **not** recorded in the header and is
still open.

### Analog outputs

```
AO0  PJ_11      AO1  PK_1       AO2  PG_7       AO3  PC_7
```

**AO2 is confirmed, and this entry used to say the opposite.** `PG_7` carries
no `tim*_ch*` function in `stm32h747xihx-pinctrl.dtsi` — only `analog`,
`dcmi_d13`, `fmc_int`, `ltdc_clk` and `sai1_mclk_a` — which read as proof that
the mapping had to be wrong. It is not. AO2 is driven by **HRTIM**, a
peripheral Zephyr's pinctrl does not model at all, and the datasheet says so
directly: see [HARDWARE_FROM_MANUAL.md](HARDWARE_FROM_MANUAL.md), section 3-7.

So the mbed library was right about this pin and the inference against it was
the error — which is worth leaving on the page about inference. An absent
alternate function in a devicetree means the binding does not exist, not that
the silicon does not.

### Digital outputs

```
DO0 PI_6   DO1 PH_9   DO2 PJ_9   DO3 PE_2
DO4 PI_3   DO5 PI_2   DO6 PD_3   DO7 PA_14
latch PB_2
```

The latch is shared across the bank. The datasheet's note that a channel "must
be toggled to activate the channel again" after a current-limit event is
presumably what it is for.

### Programmable DIO

Latch on `PH_14`. The channels themselves are **not** MCU pins — see below.

### Temperature

```
RTD CS  PA_6        TC CS   PI_0
SEL0 PD_6   SEL1 PI_4   SEL2 PG_10      TH PC_15
```

Two chip selects and a **shared 3-bit channel mux**. Note `PI_0` is also
`spi2_nss` in the Portenta H7 board definition, which is how the temperature
bus was identified as Zephyr's `spi2` — the one the Portenta datasheet calls
SPI1.

### Encoders, RS485, CAN, RTC, USB

```
ENC0  A PJ_8   B PH_12   I PH_11
ENC1  A PC_13  B PI_7    I PJ_10

RS485  TX PA_0   RX PI_9   DE PI_13  RE PI_10  EN PG_9
       RS232 PA_10  FDTERM PI_15  TERM PI_14  SLEW PG_14  HF PA_9

CAN    TX PB_8   RX PH_13  STB PA_13
RTC    INT PB_9
USB    PWR PB_14  FLAG PB_15
```

The encoder index line is transcribed as `I` because that is what the library
calls it. **This project calls it Z** — the usual name for an incremental
encoder's index or zero-marker pulse — in the shield's devicetree nodes, the
shell, `/enc.json` and the panel. Same pin, same pulse, different letter; the
library's own name is kept here because this page records what the library
believes.

Eight control GPIOs on RS485 is worth noting: the transceiver's direction,
termination, slew and protocol mode are all software-selected.

---

## The expanders, from `src/utility/ioexpander/ArduinoIOExpander.h`

**The digital inputs and the programmable channels are not MCU pins at all.**
They are bits on two TCA6424A I2C port expanders, and `pins_mc.h` does not
mention them.

```
IO_ADD  = TCA6424A_ADDRESS_ADDR_LOW     the 12 programmable channels
DIN_ADD = TCA6424A_ADDRESS_ADDR_HIGH    the 8 digital inputs
```

Which of the two carries which function is **still inferred**, from those names
alone. The addresses themselves are now measured — see
[MEASURED.md](MEASURED.md).

### The channel maps are scrambled, three different ways

A monotonic implementation would read and write the wrong channel and look
entirely plausible doing it.

| PDIO ch | write port | read port | | DIN ch | read port |
|---|---|---|---|---|---|
| 00 | P00 | P27 | | 00 | P10 |
| 01 | P01 | P26 | | 01 | P05 |
| 02 | P02 | P25 | | 02 | P04 |
| 03 | P03 | P24 | | 03 | P02 |
| 04 | **P07** | P23 | | 04 | P01 |
| 05 | **P06** | P22 | | 05 | **P00** |
| 06 | **P05** | P21 | | 06 | P07 |
| 07 | **P04** | P20 | | 07 | P06 |
| 08 | P10 | P17 | | | |
| 09 | P11 | P16 | | | |
| 10 | P12 | P15 | | | |
| 11 | P13 | P14 | | | |

Three separate non-identities:

1. **PDIO channels 4-7 have reversed write bits within the nibble.** The
   `ON_VALUE_PIN_xx` output masks agree: `04`→`0x80`, `05`→`0x40`, `06`→`0x20`,
   `07`→`0x10`.
2. **The PDIO read map descends** and uses a different port range from the
   write map. So a read-back verify must **not** reuse the write mapping —
   doing so would confirm a write against the wrong bit.
3. **The DIN map is neither** ascending nor a simple reversal.

These belong in `tools/terminals.csv` as data with a `[mbed]` provenance tag and
the upstream commit, **never open-coded at a call site**. Then each one gets
verified on the board a channel at a time, because a transcription error here
produces a panel that confidently energizes the wrong 24 V output.

### API shape

`ArduinoIOExpanderClass` wraps a `TCA6424A`: `begin()`, `set(pin, status)`,
`read(pin)`, `readAll()`, `writeAll(banks)`, `pinMode(pin, direction)`,
`toggle()`. Nothing in it is needed here — the Zephyr `ti,tca6424a` driver
exists upstream — but the constants above are.
