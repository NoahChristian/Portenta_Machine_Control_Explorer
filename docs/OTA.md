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
pwsh tools/wifi_provision.ps1   # once per board; credentials go to NVS
pwsh tools/ota.ps1
```

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

## Things that bit, recorded so they only bite once

- **`smpmgr upgrade` is composite and its pending-mark did not take.** The
  upload succeeded, the board reset, and MCUboot booted the old image with
  slot1 showing `pending=False`. The swap only happened after an explicit
  `image state-write`. Do each step and verify it; do not trust the composite.
- **`Connecting... OK` from smpmgr means nothing on UDP.** It is a local
  socket, not a reply. A board that is not on the network produces exactly the
  same line, followed by a timeout that looks like a slow handler.
- **The image must be `zephyr.signed.bin`.** The plain `zephyr.bin` has its
  vector table behind a 1 KB header that an unsigned image does not have;
  flashing it leaves a board sitting in MCUboot forever with no diagnostic.
- **dfu-util refuses to act with more than one DFU device visible**, and this
  board alone advertises four alt settings. Pass `-d vid:pid -S serial`.
