<#
.SYNOPSIS
    Install MCUboot and the signed application over the STM32 ROM bootloader.

.DESCRIPTION
    THIS SCRIPT EXISTS BECAUSE THE GIGA'S EQUIVALENT WAS LOST. That one was
    untracked, was deleted, and survives only in an editor's file history - so
    the one command nobody could reconstruct was the one that installs the
    bootloader. This file is tracked.

    Run it ONCE per board, and again only after a change to the flash map or to
    MCUboot itself. Ordinary application updates go over the network with
    tools/ota.py; they do not come through here.

    WHY THE ROM BOOTLOADER AND NOT THE ARDUINO ONE. The Arduino DFU
    bootloader's own descriptor marks internal flash sector 0 as readable but
    not writable, so it cannot be used to overwrite itself. Only the STM32's
    ROM bootloader can write 0x08000000, and it is reached by holding BOOT0 and
    tapping RST.

    NOTHING HERE CAN BRICK THE BOARD. The ROM bootloader is in silicon and
    cannot be erased, so BOOT0 + RST always works no matter what state the
    flash is left in. To go back to Arduino entirely, write the mbed core's
    bootloaders/PORTENTA_H7/bootloader.bin to 0x08000000 and rebuild the
    application with -DCONFIG_BOOTLOADER_MCUBOOT=n.

    BOTH IMAGES GO IN ONE SESSION. MCUboot at 0x08000000 and the signed
    application at 0x08040000. Doing them in two sessions means resetting
    between them, which boots a bootloader with an empty slot0.

.PARAMETER WhatIf
    Print the commands and verify the files, but write nothing.

.EXAMPLE
    # 1. Hold BOOT0, tap RST, release BOOT0. The board enumerates as 0483:df11.
    # 2. pwsh tools/install_mcuboot.ps1
#>
[CmdletBinding(SupportsShouldProcess)]
param()

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$cli = 'C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe'

$boot   = Join-Path $repo 'build\mcuboot\zephyr\zephyr.bin'
$app    = Join-Path $repo 'build\app\zephyr\zephyr.signed.bin'

# Keep equal to pmc-partitions.dtsi. Written as literals rather than parsed out
# of the dtsi deliberately: if these two ever disagree, the mistake should be
# visible in a diff of this file rather than hidden in a parser.
$bootAddr = '0x08000000'
$appAddr  = '0x08040000'
$slotSize = 0x140000

if (-not (Test-Path $cli)) {
    throw "STM32CubeProgrammer CLI not found at $cli"
}
foreach ($f in @($boot, $app)) {
    if (-not (Test-Path $f)) {
        throw "missing $f - run tools/build.ps1 -Mcuboot first"
    }
}

# --- Refuse to flash the wrong thing ----------------------------------------
#
# The plain zephyr.bin of the APPLICATION is not bootable under MCUboot: its
# vector table has to sit after MCUboot's 1 KB header, and an unsigned image
# has no header at all. Flashing it produces a board that sits in the
# bootloader forever with no diagnostic, so the signed file is checked for its
# image magic before anything is written.
$magic = [System.IO.File]::ReadAllBytes($app)[0..3]
$magicHex = ($magic | ForEach-Object { $_.ToString('x2') }) -join ''
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

Write-Host ""
Write-Host "about to write, in ONE DFU session:" -ForegroundColor Cyan
Write-Host ("  {0,-12} {1,9:N0} bytes   {2}" -f $bootAddr, (Get-Item $boot).Length, (Split-Path -Leaf $boot))
Write-Host ("  {0,-12} {1,9:N0} bytes   {2}  ({3:N1}% of slot0)" -f `
    $appAddr, $appSize, (Split-Path -Leaf $app), (100.0 * $appSize / $slotSize))
Write-Host ""
Write-Host "This REPLACES the Arduino DFU bootloader. BOOT0 + RST always recovers." -ForegroundColor Yellow
Write-Host ""

if (-not $PSCmdlet.ShouldProcess("the board over USB DFU", "write both images")) {
    Write-Host "nothing was written (-WhatIf)."
    return
}

# --- Is the ROM bootloader actually there? ----------------------------------
#
# Checked first and by itself, because "no DFU device" is the one failure with
# a one-step fix, and because every other error is easier to read once this is
# known good.
$probe = & $cli -c port=USB1 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host ($probe | Out-String)
    throw @"
No DFU device on USB1.

Hold BOOT0, tap RST, release BOOT0. The board should enumerate as USB
0483:df11 (STM Device in DFU Mode). A board running Zephyr or Arduino will NOT
appear here - that is expected; the ROM bootloader only runs when BOOT0 is held
at reset.
"@
}
$probe | Select-String -Pattern 'Device ID|Device name|BL Version|Bootloader' | ForEach-Object {
    Write-Host "  $_" -ForegroundColor DarkGray
}

# --- Write both, verify each --------------------------------------------------
#
# -v verifies the write by reading it back. Worth the seconds: an unverified
# write to the bootloader is the one that leaves no way to tell what happened.
Write-Host ""
Write-Host "==> MCUboot -> $bootAddr" -ForegroundColor Cyan
& $cli -c port=USB1 -w $boot $bootAddr -v
if ($LASTEXITCODE -ne 0) { throw "writing MCUboot failed" }

Write-Host ""
Write-Host "==> application -> $appAddr" -ForegroundColor Cyan
& $cli -c port=USB1 -w $app $appAddr -v
if ($LASTEXITCODE -ne 0) { throw "writing the application failed" }

# Start it, in the same session.
& $cli -c port=USB1 -s $bootAddr | Out-Null

Write-Host ""
Write-Host "done. Press RST (without BOOT0) if the board does not come up." -ForegroundColor Green
Write-Host ""
Write-Host "Then, over the USB CDC-ACM shell:" -ForegroundColor DarkGray
Write-Host "  pmc parts      the flash map this image resolved" -ForegroundColor DarkGray
Write-Host "  mcuboot        slot geometry, image headers, swap type" -ForegroundColor DarkGray
Write-Host "  i2c scan i2c1  expect 0x08 (the PMIC) and, if the guess is right," -ForegroundColor DarkGray
Write-Host "                 0x22 and 0x23 (the two TCA6424A expanders)" -ForegroundColor DarkGray
