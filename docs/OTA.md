# The update path

How firmware gets onto this board, and how a bad one gets off again.

## The shape of it

```
0x08000000  256K  Arduino DFU bootloader   NEVER written
0x08040000  256K  MCUboot                  chainloaded from above
0x08080000 1536K  slot0, running image
QSPI 0x100000     slot1 1536K, scratch 256K
```

MCUboot is **chainloaded rather than installed at 0x08000000**, because BOOT0
cannot be reached on this assembly — one button, no debug header, and BOOT0
only on the high-density connector the carrier occupies. `0x08040000` is where
the Arduino bootloader already jumps, and MCUboot links wherever
`boot_partition` is, so moving the partition moved the bootloader with no
MCUboot source change.

This is **safer** than replacing sector 0, not a compromise. Sector 0 is never
written — the Arduino bootloader's own DFU descriptor marks it `01*128Ka`,
readable but not writable — so it survives whatever happens above it, and
double-tap RESET always reaches it again. There is no way to brick this board
from software.

## Installing, once per board

Double-tap RESET. The board enumerates as an Arduino DFU device.

```powershell
pwsh tools/build.ps1 -Mcuboot
pwsh tools/install_mcuboot.ps1
```

The installer reads the DFU descriptor's **advertised sector layout** and
checks both writes against it before writing anything, because the failure it
guards against is a future flash-map edit that moves a partition into the
read-only sector: dfu-util would refuse mid-write, after MCUboot had already
been written, and a half-installed chain is the one state with no easy way back
on a board that cannot reach its ROM bootloader.

It also refuses an application that does not begin with MCUboot's image magic,
and a bootloader whose reset vector falls outside `boot_partition`. Both were
verified against real artifacts rather than assumed.

And it refuses to write to a board it was not told about.
`tools/bench.local.ps1`, gitignored, names the USB serial - on an STM32 the
chip's unique device ID - that the flashing tools may target. More than one
Zephyr board on this bench exposes an identical CDC-ACM shell and at least one
enumerates with the same USB VID:PID, and **DFU performs no signature check**:
dfu-util writes whatever it is given to whatever address it is given, so a
mis-targeted install overwrites another board's bootloader. The OTA path is
protected by the per-project key and by asking the board what it is running;
the DFU path has neither, so it gets this instead. Run the installer with no
bench file and it prints the serial it found and refuses to write, which is
the intended way to discover it.

## Updating, thereafter

```powershell
pwsh tools/wifi_provision.ps1       # once per board; credentials go to NVS
pwsh tools/otahttp.ps1 -Arm -Reset  # over TCP, preferred
pwsh tools/ota.ps1                  # over UDP, the SMP path
```

### Prefer the HTTP path, and the reason is retransmission

`tools/ota.ps1` drives `smpmgr` over SMP/UDP, which is **lockstep with no
window**: one datagram lost in either direction ends the transfer, because the
protocol has nothing to resend it with. Measured on this bench on 2026-11-18,
loss is about **0.06% per datagram** - one in 1,600, invisible to a ping test
and fatal to a 1,100-datagram upload roughly half the time. On one occasion
five consecutive attempts failed. `docs/MEASURED.md` has the capture.

`tools/otahttp.ps1` posts the same signed image to `POST /ota` on the panel's
own server, over TCP, which resends what it loses. Measured in the same
session: **260 kB/s** best and 80 kB/s typical, against 70 kB/s for a
*successful* UDP upload, and it has not yet failed.

Zephyr has no TCP transport for SMP - nine transports, none of them TCP - so
this is a route of ours rather than a configuration change. It writes into the
secondary slot through the same `flash_img` API that `img_mgmt` uses, so
MCUboot's slot logic and signature check are untouched. **Only the transport is
different.**

```
POST /ota     body = build/app/zephyr/zephyr.signed.bin
              header X-PMC-Token: <the API token>
              -> "staged <n> bytes"
GET  /ota     -> "staged <n> bytes", the count already in slot1
POST /io      token=<...>&do=otatest     arms it
POST /reset   token=<...>                swaps on the next boot
```

**Staging and arming are two acts on purpose.** Writing slot1 changes nothing
about what the board boots; arming does. Keeping them apart means a
half-considered upload cannot reboot the board, and it mirrors smpmgr's own
`upload` / `state-write` split.

The uploader checks the **board's** reported byte count against what it sent
and refuses to arm on a mismatch. An HTTP 200 means the request completed; it
does not mean every byte reached flash.

Uploaded in **test** mode. MCUboot swaps slot1 into slot0 through the scratch
area and boots it; if the image does not confirm itself, the next reset puts
the old one back. A bad image costs one reboot, not a trip to the bench.

The confirming is done by the **health gate** (`app/src/health.c`), not by the
upload tool and not by `--confirm`. So "the update stuck" means the image
proved its I/O answers and that it is reachable — not merely that bytes
arrived. `health` on the shell says which condition is failing, because an
image that will not confirm must say why.

## The keys

A **per-project** ECDSA P-256 key, kept outside every repository in
`Documents/Arduino/mcuboot-signing/`. Proven in both directions with
`imgtool verify`: the correct key reports "Image was correctly validated", and
a sibling project's key on this bench reports "No signature found for the given
key". So an image built for another board here cannot install on this one.

Note what MCUboot does **not** offer: a per-project trailer magic.
`BOOT_IMG_MAGIC` is a constant in `bootutil`, not a Kconfig. The key is the
real control, and a product-ID check in the confirm gate is the intended other
half.

`mcuboot/pmc.conf` carries a **fail-closed placeholder** for the key path:

```
CONFIG_BOOT_SIGNATURE_KEY_FILE="MISSING-pmc-key.conf--see-mcuboot-README.pem"
```

overridden by the gitignored `pmc-key.conf`, listed **last** on the command
line so it wins. This matters more than it looks: MCUboot's own ECDSA-P256
default is `root-ec-p256.pem`, which is **committed in the MCUboot repository
and therefore published** — anyone can sign an image a bootloader built with it
will accept. And falling back to it is easy to do by accident, because
`pmc-key.conf` is gitignored and `git clean -xdf` deletes it.

Proved by planting the failure: building with `pmc.conf` alone fails with

```
ninja: error: '...MISSING-pmc-key.conf--see-mcuboot-README.pem',
needed by 'zephyr/autogen-pubkey.c', missing and no known rule to make it
```

and produces **no bootloader**. On the sibling project the same fallback
succeeded with one warning line among hundreds and produced a bootloader that
flashed and ran.

### Custody

Generated with

```
imgtool keygen -k <dir>/pmc-explorer-ec-p256.pem -t ecdsa-p256
```

**Back it up.** Losing it means no future image can be signed for a board
already carrying this bootloader, and the way back is a full reinstall over
double-tap RESET - *not* BOOT0, which this assembly cannot reach at all.

The application half is unguarded on purpose. `app/mcuboot-key.conf` names the
key imgtool signs with, and it must name the same key the bootloader verifies
against; if it does not, the bootloader refuses the image, which is a visible
and safe failure. The bootloader half carries the fail-closed placeholder
because its failure mode is the opposite: silent, and trusting a published key.

## Recovery

**One failed swap silently disables OTA forever** until it is cleared, and this
is the failure mode most worth knowing. MCUboot writes `swap_info` into the
**primary** slot's trailer before it starts moving sectors. If the swap dies
partway, that byte stays, and every later boot takes the "resume a swap I
already decided on" branch instead of consulting `boot_swap_tables` — so a
freshly staged update is **ignored and left pending**. The only outward sign is
a missing log line from a bootloader that has no console.

```
swapclear confirm
```

erases the scratch area, slot1's last sector **and slot0's last sector**. The
third is the one that is easy to omit. Each erase length is read from that
area's own geometry, not hardcoded — observed in practice:

```
scratch: erased 0x40000 (all)
slot1  : erased 0x1000  at +0x17f000      4 KB, QSPI
slot0  : erased 0x20000 at +0x160000      128 KB, internal
```

Two different erase units from one command. A hardcoded 64 KB would have been
legal internally, rejected on QSPI, and would have left the slot0 trailer
uncleared — i.e. exactly the failure the command exists to undo.

`mcuboot` on the shell prints what the bootloader decides from: slot geometry,
image headers against magic `0x96f3b83d`, and the swap type. It exists because
the shipping bootloader is built with no console and declines in silence.

## How the bootloader is configured

`mcuboot/pmc.conf`, and why each choice is the one it is.

**swap-using-scratch, never swap-using-move.** The two slots have
heterogeneous geometry - 128 KB sectors in internal flash against 4 KB in the
QSPI NOR, so 10 sectors against 320. `boot_slots_compatible()` requires equal
sector counts *and* equal sector sizes, move fails both, and it **fails
silently**: the update uploads, marks pending, the board resets, and MCUboot
boots the old image with no error anywhere. Overwrite-only would be smaller
again and would remove the way back from an image that does not boot, which is
the one property this has to keep.

It is set explicitly even though it may already be the default. MCUboot's
choice defaults to scratch when a `scratch_partition` nodelabel is enabled,
and the Portenta board does enable one - internally, at `0x08010000`, sharing
erase sector 0 with the bootloader. Relying on that default would be relying
on the right answer for the wrong reason, derived from a node this project
deletes.

**The sector ceiling covers the secondary slot, not the primary.** 320 QSPI
sectors, not 10 internal ones. `BOOT_MAX_IMG_SECTORS_AUTO` derives it from the
first image's partition, which on an asymmetric layout is the wrong end. It
must also stay equal to the `zephyr_compile_definitions()` value in
`app/CMakeLists.txt`: the application writes the image trailer through
`img_mgmt`, so it has to agree about where that trailer begins, and the symbol
exists only in MCUboot's Kconfig and never in the application's.

**ECDSA P-256** for smaller signatures and smaller verification code, and
because a sibling port on this bench already uses it - one less difference
between two boards.

## Things that bit, recorded so they only bite once

- **`smpmgr upgrade` is composite and its pending-mark did not take.** The
  upload succeeded, the board reset, and MCUboot booted the old image with
  slot1 showing `pending=False`. The swap only happened after an explicit
  `image state-write`. Do each step and verify it; do not trust the composite.
- **"Did the bytes arrive?" has to be asked by hash.** `ota.ps1` once accepted
  a version string appearing anywhere in `image state-read`, which passes on a
  board that received nothing, and `-Resume` skipped the question outright. A
  slot reports a plausible version while only part way written, because the
  MCUboot header lands in the first kilobyte. The hash is computed from the
  local file - header plus `ih_img_size` plus protected TLVs, stopping short of
  the signature - and compared against the slot's. `docs/MEASURED.md` has the
  three-way verification and the live refusal.
- **`Connecting... OK` from smpmgr means nothing on UDP.** It is a local
  socket, not a reply. A board that is not on the network produces exactly the
  same line, followed by a timeout that looks like a slow handler.
- **The image must be `zephyr.signed.bin`.** The plain `zephyr.bin` has its
  vector table behind a 1 KB header that an unsigned image does not have;
  flashing it leaves a board sitting in MCUboot forever with no diagnostic.
- **dfu-util refuses to act with more than one DFU device visible**, and this
  board alone advertises four alt settings. Pass `-d vid:pid -S serial`.
- **It must be `BOARD_SERIAL_BACKEND_CDC_ACM=n`, not merely `UART_CONSOLE=n`.**
  This board sources `boards/common/usb/Kconfig.cdc_acm_serial.defconfig` and
  chooses `board_cdc_acm_uart` as console, shell and mcumgr UART. Turning off
  only `UART_CONSOLE` leaves `uart_console.c` compiled against a console device
  MCUboot never instantiates, and the **link** fails on an undefined
  `__device_dts_ord_NNN`.
- **Not `CONFIG_LOG=n` in the bootloader.** Turning logging off outright fights
  MCUboot's own config, which sets `MCUBOOT_LOG_LEVEL_INF` and
  `LOG_MODE_MINIMAL`, and produced three Kconfig warnings on every build.
  Warnings on every build are how a real warning gets missed, and the logging
  costs nothing when `CONFIG_CONSOLE=n` leaves it no backend to reach.
- **`CONFIG_SPI` is not how the QSPI NOR is reached.** It is driven by
  `FLASH_STM32_QSPI` on the QUADSPI peripheral and does not go through the SPI
  subsystem at all. Enabling SPI produced only `No SOURCES given to Zephyr
  library: drivers__spi` - an empty library, which is the tell that a symbol is
  doing nothing.
