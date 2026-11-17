# Portenta Machine Control Explorer

A browser-served diagnostic panel for the Arduino Portenta Machine Control
(AKX00032), running native Zephyr with dual-slot MCUboot updates over WiFi.

## Why

The Machine Control is a capable industrial carrier — 8 digital inputs, 8
high-side digital outputs, 12 programmable digital channels, 3
software-configurable analog inputs, 4 analog outputs, 3 temperature channels,
2 encoders, RS485/RS232, CAN, RTC — and it is **poorly documented**. The
datasheet gives connector tables and ratings but not operational procedure: it
does not say how an analog input's 0-10 V / 4-20 mA / NTC mode is selected,
what the latch pins do, or how three temperature channels share one SPI bus
between two different front-end chips.

The only real documentation is the `Arduino_PortentaMachineControl` library,
which is mbed-only — a core Arm declared end-of-life in July 2026.

So this is an instrument for making the hardware legible: a panel that reads
and writes every named channel, with the names an Arduino user already knows,
and a repository that records how each fact was established. Every claim here
is tagged by how much it is worth trusting — see
[the documents](#the-documents).

## Status

![The panel, with a connector line from a PDIO control to its terminal](docs/images/drive-panel-link.png)

Hovering a control draws a line to the terminal it drives, so the association
between a button and a screw terminal is visible rather than remembered. The
terminals are found by the same `?ch=<name>` key the firmware's allow-list
uses, so a channel that can be clicked can be linked, with no second list to
keep in step.

Working on hardware:

| | |
|---|---|
| Boot chain | Arduino bootloader → MCUboot → Zephyr, all verified on the board |
| Flash map | Custom, dual-slot, confirmed against the running image with `pmc parts` |
| OTA | Upload, swap and boot over WiFi, proven end to end |
| Discovery | `portentaexplorer.local` |
| Reads | 8 digital in, 12 programmable digital I/O, 3 analog in updating every 2 s |
| Writes | 8 digital out (level and pulse), 12 PDIO, 3 analog out — token-gated |
| Supply | 24 V presence inferred from the encoder pull-ups; 3V3 and 12 V indicated |
| Clock | PCF8563 set from NTP, carried through power-off by the on-board supercap |
| Panel | Rendered on the board from `web/index.html`, with a drawing of the carrier whose terminals carry live state |

**Each write says only what it can prove.** A digital output reports
`commanded`, because that family has no sense path and nothing on the board
can confirm it. A programmable channel reports `verified`, having pulsed the
latch and read the same channel back as an input — or `no supply`, when it
reads low with no fault flag, which means the `24V IN` terminal is unfed
rather than that anything failed. An analog output reports the value it set,
and says the volts-per-count is derived rather than measured.

Not built yet: temperature, encoder counts, RS485 and CAN. Only one channel
has been confirmed at the connector with a meter, so the rest of the board
drawing renders dashed — the expander channel maps are transcribed, not yet
proven here.

### The panel is a template the board renders, not a page a script assembles

It is server-side includes, through the [HtmlTpl][lib] engine shared with a
sibling project on this bench. The page is authored as `web/index.html` and
opens in a browser as a file;
`tools/gen_page.py` checks every `<!--#v name-->` in it against the registry
in `app/src/page_vars.cpp` and emits the committed header the firmware
serves. A name the registry does not have is a build error naming the line.

Two consequences worth stating, because each replaced something worse:

- **Every reading on the page comes from one instant.** The renderer takes a
  single snapshot before the first row is asked for, which is what makes the
  sentence in the page's own heading true. Nothing polls; reload to take a
  fresh set.
- **One script, and it talks to nobody.** The clock marker is rendered by the
  board, digits and all, and a few lines at the end of the page advance them
  from the browser's own elapsed time. It issues no request, so the page still
  takes every reading in one pass and still never polls. With the script
  removed the marker shows the time the board held when it rendered.
- **The board drawing is one terminal, written once.** All 79 are the same
  markup inside an `each` block, and `tools/gen_board.py` solves where each one
  sits from `tools/terminals.csv`. That is also the only way the hover tips can
  be live: 79 terminals cannot be 79 registry variables.

[lib]: https://github.com/NoahChristian/Arduino_HTTP_HTML

## Hardware

The board has **no display and no software-controllable LED** — the only LED
the datasheet mentions is a hard-wired power indicator that does not change on
reset. So the USB CDC-ACM shell and, later, the web panel are the only two ways
to see anything, and `DO1` is driven as a boot indicator: high from early boot,
low once every health check passes. `d1 walk` steps through all eight outputs
if the lit one is not the one you expect.

It also has **no BOOT0 button, no SWD header and no JTAG header**. Its
topology table lists exactly one button, `PB1 RESET`, and the Portenta H7's own
schematic has exactly one too. BOOT0 exists only on the H7's high-density
connector, which the carrier occupies. That single fact shapes the whole
install path — see [docs/MEASURED.md](docs/MEASURED.md).

## The secure element, and why this project does not touch it

The Machine Control datasheet lists an **NXP SE0502 secure element**, and the
Portenta H7 schematic carries a `Cryptochip` sheet with both an `SE050C2HQ1`
and an `ATECC608A` — alternates across board revisions. Zephyr's board
definition describes neither, and an I2C scan here found neither at its default
address (0x48 for the SE050, 0x60 for the ATECC608), so which part is fitted
and where it sits is an open question.

**This project deliberately leaves it alone**, for two reasons. The second is
the interesting one.

The first is that the damage would be permanent. The ATECC608's config and
data zones lock **once, irreversibly**. An open-source exploration tool has no
business bricking a capability its users may want later, and "we only meant to
look" is not a recovery plan.

The second is that the part is sold as a feature of the board and is largely
not yours. It is advertised as the secure element that makes this a secure IoT
device, and then the vendor's own cloud provisioning claims its configuration
for an identity tied to their subscription service. There is a legitimate
engineering reason for that — a device identity a cloud can trust has to be
provisioned somewhere trustworthy, and the factory is the obvious place — but
the effect is that a hardware capability you paid for arrives with someone
else's key in it, and taking it back is a one-way door that forecloses the use
it was advertised for. Good engineering, annexed by a licensing model.

Worth being clear-eyed about, because it changes what the part is *for* on a
board like this one: not a general-purpose key store you happen to own, but a
vendor identity anchor you may borrow. That is a different component, and it
should be reasoned about as one.

What it would buy, if that trade were ever worth making, is narrower than it
first appears:

- **Not image verification.** MCUboot verifies with a *public* key, which needs
  integrity rather than secrecy, and compiling it into the bootloader already
  provides that. A secure element adds very little here.
- **Image encryption**, which is the real argument. MCUboot's ECIES mode puts a
  *private* decryption key on the device. That is a genuine secret, and today
  it would sit in flash — readable over DFU, because the Arduino bootloader
  marks only sector 0 read-only and leaves the rest readable.
- **Anti-rollback**, where a hardware monotonic counter beats a flash-backed
  security counter.
- **Device identity** for mutual TLS to an update server.

And for a fleet, the thing that matters most is **per-device keys rather than a
shared secret**. A secure element is good at exactly that — a key that cannot
be copied off the part. But trusting the *factory* cert chain is weaker than it
sounds, because any board carrying that chain would authenticate. Fleet
separation has to come from keys you control, not from the vendor's.

This project gets its cross-device separation from a **per-project MCUboot
signing key** instead, which costs nothing and is reversible: an image signed
for another board on this bench is refused here, proven with `imgtool verify`
in both directions. See [docs/OTA.md](docs/OTA.md).

## Building

Needs a Zephyr west workspace, the Zephyr SDK, and a checkout of
**Arduino_HTTP_HTML** beside this repository — the panel is a template the
board renders, so the engine is a build dependency, not an optional extra.
Override its location with `-DHTMLTPL_DIR=<path>` or `$env:HTMLTPL_DIR`.

Every argument in the build script was arrived at by a build failing in a way
that did not name the cause, which is why there is a script rather than a
command to copy:

```powershell
python tools/gen_board.py       # only after editing tools/terminals.csv
python tools/gen_page.py        # only after editing web/ or the registry
pwsh tools/build.ps1            # application only
pwsh tools/build.ps1 -Mcuboot   # bootloader too; needs its own signing key
```

Both generated files are **committed**, so the firmware builds without Python;
the pre-commit hook runs each generator with `--check` so a stale one cannot
reach a commit. A stale generated page compiles clean, flashes clean, and
serves the previous page.

Pinned toolchain, because the HTTP server is marked experimental upstream and
several Kconfig symbols moved recently: Zephyr 4.5.99, SDK 1.0.1,
arm-zephyr-eabi gcc 14.3.0.

## Installing, once

MCUboot is **chainloaded** rather than installed over the Arduino bootloader,
because BOOT0 cannot be reached on this assembly:

```
0x08000000  256K  Arduino DFU bootloader   never written
0x08040000  256K  MCUboot
0x08080000 1536K  slot0, the application
```

Double-tap RESET, then:

```powershell
pwsh tools/install_mcuboot.ps1
```

It reads the DFU descriptor's sector layout and checks both writes against it
before writing anything, refuses an application that is not a signed MCUboot
image, and refuses a bootloader whose reset vector falls outside
`boot_partition`.

Nothing here can brick the board: sector 0 is never written, so the Arduino
bootloader survives whatever happens above it and double-tap RESET always
reaches it again.

## Updating, thereafter

```powershell
pwsh tools/wifi_provision.ps1   # once per board
pwsh tools/ota.ps1
```

Credentials are **not in the firmware**. They are pushed once over the USB
shell into the `kvs` NVS partition, which sits outside both image slots and so
survives every update. A leaked image leaks no credential, and
`app_secrets.h.example` carries placeholders only — generated by blanking every
value, never by copying a working file.

An update is uploaded in **test** mode. MCUboot swaps it in, and if the new
image does not confirm itself the next reset puts the old one back, so a bad
image costs one reboot rather than a trip to the bench. The confirming is done
by the health gate, not by the upload tool, so "the update stuck" means the
image proved its I/O answers. `health` says which condition is failing.

## The documents

Split by **kind of confidence**, and a fact has exactly one home. Moving it up
the ladder means deleting it from the page below, not copying it.

| | |
|---|---|
| [docs/HARDWARE_FROM_MANUAL.md](docs/HARDWARE_FROM_MANUAL.md) | read from a datasheet; never seen on hardware |
| [docs/INFERRED_FROM_MBED.md](docs/INFERRED_FROM_MBED.md) | what another program believes; mbed-only, not authoritative about silicon |
| [docs/MEASURED.md](docs/MEASURED.md) | observed here, with the command that produced it |

Captured output is pasted verbatim and never retyped into a table — a table is
something a person composed, and the value of captured output is that nobody
did.

## License

Split, deliberately:

- **`boards/shields/` and `pmc-partitions.dtsi` are Apache-2.0.** The shield is
  the part with community value — `-DSHIELD=arduino_portenta_machine_control`
  is what someone else would want — and Zephyr requires Apache-2.0 for upstream
  contributions, so this keeps that door open. The partition file is derived
  from the Giga port's Apache-2.0 original.
- **Everything else is MIT**, matching the other projects on this bench.

See [LICENSE](LICENSE).
