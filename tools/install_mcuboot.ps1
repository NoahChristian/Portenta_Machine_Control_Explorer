<#
.SYNOPSIS
    Install MCUboot and the signed application through the Arduino DFU
    bootloader, reached by double-tapping RESET.

.DESCRIPTION
    THIS SCRIPT EXISTS BECAUSE THE GIGA'S EQUIVALENT WAS LOST. That one was
    untracked, was deleted, and survives only in an editor's file history - so
    the one command nobody could reconstruct was the one that installs the
    bootloader. This file is tracked.

    Run it ONCE per board, and again only after a change to the flash map or to
    MCUboot itself. Ordinary application updates go over the network with
    tools/ota.py; they do not come through here.

    WHY NOT BOOT0 AND THE ROM BOOTLOADER, which is how the Giga does it. BOOT0
    cannot be reached on this assembly. The Machine Control's datasheet lists
    exactly one button - `PB1 RESET Push Button` - and names no SWD, JTAG,
    debug or BOOT header anywhere in its topology table. The Portenta H7's own
    schematic has exactly one push button as well (PB1, pin refs PIPB105 and
    PIPB106), which is its reset. BOOT0 exists only as a pin on the H7's
    high-density connector, and the carrier occupies that connector.

    So MCUboot is CHAINLOADED rather than installed in place:

        0x08000000  Arduino DFU bootloader   never written
        0x08040000  MCUboot                  the Arduino bootloader jumps here
        0x08080000  slot0, the application

    Everything above sector 0 is writable through the Arduino bootloader's own
    DFU interface, whose descriptor marks only sector 0 read-only
    (01*128Ka,15*128Kg - the trailing `a` means readable, `g` means
    readable-writable-erasable). So nothing here needs a jumper, a probe, or a
    disassembled enclosure.

    NOTHING HERE CAN BRICK THE BOARD, and the reason is stronger than on the
    Giga: sector 0 is never written, so the Arduino bootloader survives
    whatever happens above it, and double-tap RESET always reaches it again.

    BOTH IMAGES GO IN ONE SESSION. Doing them separately means leaving the
    board between writes with a bootloader whose slot0 is empty or stale.

.PARAMETER WhatIf
    Verify the files and print the commands, but write nothing.

.EXAMPLE
    # 1. Double-tap the RESET button. The board re-enumerates as a DFU device.
    # 2. pwsh tools/install_mcuboot.ps1
#>
[CmdletBinding(SupportsShouldProcess)]
param()

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot

# dfu-util ships with the Arduino IDE. Resolved by search rather than pinned to
# one version directory, because that directory name carries a build hash.
$dfu = Get-ChildItem -Path (Join-Path $HOME 'AppData\Local\Arduino15') -Recurse `
        -Filter 'dfu-util.exe' -ErrorAction SilentlyContinue |
       Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
if (-not $dfu) {
    throw "dfu-util.exe not found under Arduino15. Install the Arduino IDE, or put dfu-util on PATH."
}

$boot = Join-Path $repo 'build\mcuboot\zephyr\zephyr.bin'
$app  = Join-Path $repo 'build\app\zephyr\zephyr.signed.bin'

# Keep equal to pmc-partitions.dtsi. Written as literals rather than parsed out
# of the dtsi deliberately: if these two ever disagree, the mistake should be
# visible in a diff of this file rather than hidden in a parser.
$bootAddr = '0x08040000'
$appAddr  = '0x08080000'
$slotSize = 0x180000

foreach ($f in @($boot, $app)) {
    if (-not (Test-Path $f)) {
        throw "missing $f - run tools/build.ps1 -Mcuboot first"
    }
}

# --- Refuse to flash the wrong thing ----------------------------------------
#
# The plain zephyr.bin of the APPLICATION is not bootable under MCUboot: its
# vector table sits behind a 1 KB header that an unsigned image does not have.
# Flashing it leaves a board sitting in MCUboot forever with no diagnostic, so
# the image magic is checked before anything is written.
$magicBytes = [System.IO.File]::ReadAllBytes($app)[0..3]
$magicHex = ($magicBytes | ForEach-Object { $_.ToString('x2') }) -join ''
if ($magicHex -ne '3db8f396') {
    throw @"
$app does not start with MCUboot's image magic (got 0x$magicHex, expected
0x3db8f396 little-endian for 0x96f3b83d). That is not a signed image. Check
that CONFIG_BOOTLOADER_MCUBOOT=y and that the signing key fragment is present.
"@
}

$appSize = (Get-Item $app).Length
if ($appSize -ge $slotSize) {
    throw "signed image is $appSize bytes but slot0 is $slotSize - it cannot fit"
}

# MCUboot's own image is a raw vector table, and its reset vector must point
# inside boot_partition. If it does not, boot_partition was moved without
# rebuilding, and the Arduino bootloader would jump into nothing.
$bootHead = [System.IO.File]::ReadAllBytes($boot)[0..7]
$bootPc = [BitConverter]::ToUInt32($bootHead, 4)
if ($bootPc -lt 0x08040000 -or $bootPc -ge 0x08080000) {
    throw ("MCUboot's reset vector is 0x{0:x8}, outside boot_partition at {1}. " +
           "Rebuild: the bootloader and the flash map disagree." -f $bootPc, $bootAddr)
}

Write-Host ""
Write-Host "about to write, in ONE DFU session:" -ForegroundColor Cyan
Write-Host ("  {0,-12} {1,9:N0} bytes   {2,-18} reset vector 0x{3:x8}" -f `
    $bootAddr, (Get-Item $boot).Length, 'MCUboot', $bootPc)
Write-Host ("  {0,-12} {1,9:N0} bytes   {2,-18} {3:N1}% of slot0" -f `
    $appAddr, $appSize, 'signed application', (100.0 * $appSize / $slotSize))
Write-Host ""
Write-Host "Sector 0 is NOT touched: the Arduino bootloader stays, and double-tap" -ForegroundColor DarkGray
Write-Host "RESET always reaches it again. Nothing here can brick the board." -ForegroundColor DarkGray
Write-Host ""

# --- Is the board in DFU mode, and what will it let us write? ---------------
#
# Listed and READ before anything is written. The memory layout string in the
# alt-setting name is the board telling you which sectors it will accept, and
# it is the only way to know that without guessing. Reading something known
# before writing anything is the same discipline the bench's J-Link notes
# insist on.
Write-Host "==> DFU devices present" -ForegroundColor Cyan
$list = & $dfu -l 2>&1 | Out-String
$alts = $list -split "`n" | Where-Object { $_ -match 'Found DFU' }
$serials = @($alts | ForEach-Object {
    if ($_ -match 'serial="([^"]+)"') { $Matches[1] }
} | Select-Object -Unique)
if (-not $alts) {
    Write-Host $list
    throw @"
No DFU device found.

Double-tap the RESET button - two quick presses. The board should re-enumerate
as an Arduino DFU device (VID 0x2341; the Portenta's runtime PID is 0x025b, so
expect something like 0x035b in DFU mode, following the same convention the
Giga uses). A board running Zephyr or an Arduino sketch will NOT appear here.

If a single tap resets it but a double tap does not enter DFU, try varying the
gap between taps - the window is short.
"@
}
$alts | ForEach-Object { Write-Host "  $($_.Trim())" -ForegroundColor DarkGray }

# --- WHICH BOARD IS THIS? ----------------------------------------------------
#
# THE MOST IMPORTANT CHECK IN THIS SCRIPT, and the one that was missing.
#
# DFU HAS NO SIGNATURE CHECK. dfu-util writes whatever it is given to whatever
# address it is given; MCUboot's key only protects the OTA path, which is why
# a mis-aimed OTA is merely refused while a mis-aimed DFU overwrites another
# board's bootloader.
#
# And VID:PID DOES NOT IDENTIFY THE BOARD. More than one Zephyr board lives on
# this bench, they expose identical CDC-ACM shells, and at least one of them
# enumerates with this same VID:PID in some states. Earlier in this project a
# tool picked a port by position and asked the WRONG BOARD for its address.
#
# The USB serial is the STM32's unique device ID, which is a real
# discriminator. It is compared against tools/bench.local.ps1 - gitignored,
# because a unique ID is a device identifier in the same class as a MAC.
#
# FAIL-CLOSED: with no bench file, this prints the serial and REFUSES. The
# alternative - writing to whatever answered - is exactly the behavior being
# removed.
if ($serials.Count -gt 1) {
    throw ("More than one DFU device is present ({0}). Disconnect all but the " +
           "target: this script will not guess." -f ($serials -join ', '))
}
$found = $serials[0]

$benchFile = Join-Path $PSScriptRoot 'bench.local.ps1'
if (Test-Path $benchFile) { . $benchFile }

if (-not $PMC_DFU_SERIAL) {
    throw @"
No expected board recorded, so this script will not write.

The DFU device present has serial:

    $found

If that is the Portenta Machine Control and not another board on this bench,
copy tools/bench.local.ps1.example to tools/bench.local.ps1 and set

    `$PMC_DFU_SERIAL = '$found'

That file is gitignored. DFU performs no signature check, so this is the only
thing standing between a typo and another board's bootloader.
"@
}

if ($found -ne $PMC_DFU_SERIAL) {
    throw @"
WRONG BOARD. Refusing to write.

    expected  $PMC_DFU_SERIAL   (tools/bench.local.ps1)
    found     $found

DFU writes with no signature check, so continuing would overwrite whatever
this board is. If you have genuinely replaced the Portenta, update
tools/bench.local.ps1 deliberately.
"@
}
Write-Host "  board identity matches tools/bench.local.ps1" -ForegroundColor DarkGray

# The internal-flash alt setting advertises its sector layout. Confirm the
# addresses we are about to write fall in a WRITABLE region rather than
# assuming it.
$internal = $alts | Where-Object { $_ -match 'Internal Flash' } | Select-Object -First 1
if (-not $internal) {
    throw "no 'Internal Flash' alt setting was advertised - check the listing above"
}
if ($internal -notmatch '/0x([0-9A-Fa-f]+)/([0-9A-Za-z\*,]+)') {
    throw "could not parse the internal-flash sector layout from: $internal"
}

# PARSE THE LAYOUT AND CHECK OUR TARGETS AGAINST IT.
#
# The descriptor is the board stating which sectors it will accept, in DfuSe
# form: 01*128Ka,15*128Kg means one 128 KB sector that is readable only
# followed by fifteen that are readable, writable and erasable. Sector 0 being
# 'a' is precisely why MCUboot is chainloaded rather than installed at
# 0x08000000.
#
# Checked rather than assumed, because the failure it guards against is a
# future edit to pmc-partitions.dtsi that moves a partition down into sector 0.
# dfu-util would then refuse mid-write, after MCUboot had already been written
# somewhere - and a half-installed chain is the one state with no obvious way
# back short of the ROM bootloader this board cannot reach.
# IDENTIFY THE DEVICE EXACTLY, and write only to that one.
#
# dfu-util refuses to act when it can see more than one DFU-capable device,
# and this board alone advertises four alt settings - plus whatever else on the
# machine happens to expose a DFU interface (a webcam did, here). Passing
# -d vid:pid and -S serial means the writes land on the device whose descriptor
# was just inspected, rather than on whichever one dfu-util would have picked.
if ($internal -match '\[([0-9A-Fa-f]{4}):([0-9A-Fa-f]{4})\]') {
    $devId = "$($Matches[1]):$($Matches[2])"
} else {
    throw "could not read vid:pid from: $internal"
}
if ($internal -match 'serial="([^"]+)"') {
    $devSerial = $Matches[1]
} else {
    throw "could not read the serial number from: $internal"
}

if ($internal -notmatch '/0x([0-9A-Fa-f]+)/([0-9A-Za-z\*,]+)') {
    throw "could not re-parse the internal-flash sector layout"
}
$base = [Convert]::ToUInt32($Matches[1], 16)
$regions = @()
$cursor = $base
foreach ($seg in $Matches[2] -split ',') {
    if ($seg -notmatch '^(\d+)\*(\d+)([KMB])([a-g])$') {
        throw "unrecognized sector segment '$seg' in layout $($Matches[2])"
    }
    $count = [int]$Matches[1]
    $unit = switch ($Matches[3]) { 'K' { 1KB } 'M' { 1MB } default { 1 } }
    $size = [int]$Matches[2] * $unit
    # DfuSe attribute letters: a=readable, b/c/d add erase/write bits, e/f/g
    # are the writable combinations. Anything other than 'a' can be written.
    $writable = ($Matches[4] -ne 'a')
    $regions += [pscustomobject]@{
        Start = $cursor; End = $cursor + ($count * $size); Writable = $writable; Spec = $seg
    }
    $cursor += $count * $size
}

Write-Host ""
Write-Host "  internal flash, as the board describes it:" -ForegroundColor DarkGray
foreach ($r in $regions) {
    $w = if ($r.Writable) { 'writable' } else { 'READ-ONLY' }
    Write-Host ("    0x{0:x8} .. 0x{1:x8}  {2,-9}  {3}" -f $r.Start, $r.End, $w, $r.Spec) -ForegroundColor DarkGray
}

foreach ($t in @(@{N='MCUboot'; A=[Convert]::ToUInt32($bootAddr,16); S=(Get-Item $boot).Length},
                 @{N='application'; A=[Convert]::ToUInt32($appAddr,16); S=$appSize})) {
    $first = $regions | Where-Object { $t.A -ge $_.Start -and $t.A -lt $_.End } | Select-Object -First 1
    $last  = $regions | Where-Object { ($t.A + $t.S - 1) -ge $_.Start -and ($t.A + $t.S - 1) -lt $_.End } | Select-Object -First 1
    if (-not $first -or -not $last) {
        throw ("{0} at 0x{1:x8} for {2} bytes falls outside the flash this board advertises" -f $t.N, $t.A, $t.S)
    }
    $spanned = $regions | Where-Object { $_.End -gt $t.A -and $_.Start -lt ($t.A + $t.S) }
    $ro = $spanned | Where-Object { -not $_.Writable }
    if ($ro) {
        throw ("{0} at 0x{1:x8} for {2} bytes would write the READ-ONLY region " +
               "0x{3:x8}..0x{4:x8}. The board will refuse it. Check the flash map." -f `
               $t.N, $t.A, $t.S, $ro[0].Start, $ro[0].End)
    }
    Write-Host ("  {0,-12} 0x{1:x8} + {2,9:N0} bytes: all in writable flash" -f $t.N, $t.A, $t.S) -ForegroundColor DarkGray
}

if (-not $PSCmdlet.ShouldProcess("the board over Arduino DFU", "write MCUboot and the application")) {
    Write-Host ""
    Write-Host "nothing was written (-WhatIf)."
    return
}

# --- Write both -------------------------------------------------------------
#
# -a 0 selects the internal-flash alt setting. The DfuSe address syntax puts
# the destination in -s. Only the LAST write takes :leave, so the board stays
# in DFU between the two.
Write-Host ""
Write-Host "  writing to $devId serial $devSerial" -ForegroundColor DarkGray
Write-Host ""
Write-Host "==> MCUboot -> $bootAddr" -ForegroundColor Cyan
& $dfu -d $devId -S $devSerial -a 0 -s "$bootAddr" -D $boot
if ($LASTEXITCODE -ne 0) { throw "writing MCUboot failed" }

Write-Host ""
Write-Host "==> application -> $appAddr" -ForegroundColor Cyan
& $dfu -d $devId -S $devSerial -a 0 -s "${appAddr}:leave" -D $app
if ($LASTEXITCODE -ne 0) { throw "writing the application failed" }

Write-Host ""
Write-Host "done." -ForegroundColor Green
Write-Host ""
Write-Host "Then, over the USB CDC-ACM shell:" -ForegroundColor DarkGray
Write-Host "  pmc parts      the flash map this image resolved - check it against" -ForegroundColor DarkGray
Write-Host "                 pmc-partitions.dtsi, because a node deletion that did" -ForegroundColor DarkGray
Write-Host "                 not take is invisible otherwise" -ForegroundColor DarkGray
Write-Host "  mcuboot        slot geometry, image headers, swap type" -ForegroundColor DarkGray
Write-Host "  i2c scan i2c1  expect 0x08 (the PF1550 PMIC) and, if the guess about" -ForegroundColor DarkGray
Write-Host "                 the expanders is right, 0x22 and 0x23" -ForegroundColor DarkGray
