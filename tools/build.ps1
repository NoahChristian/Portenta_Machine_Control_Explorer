<#
.SYNOPSIS
    Build the Portenta Machine Control Explorer application, and optionally
    MCUboot.

.DESCRIPTION
    THE INVOCATION IS THE POINT OF THIS SCRIPT. Every argument below was
    arrived at by a build failing in a way that did not name the cause, and
    none of it is guessable:

      -DBOARD_ROOT, not -DSHIELD_ROOT. Shields are discovered under
      $BOARD_ROOT/boards/shields/. SHIELD_ROOT does not exist in this Zephyr
      and is accepted silently on the command line, after which the shield is
      "not found" and the error lists four unrelated shields.

      The venv's Scripts directory must be on PATH, because cmake and ninja
      live there and nowhere else on this machine. Without it west reports
      "CMake is not installed or cannot be found", which sounds like a missing
      install rather than a missing PATH entry.

      ZEPHYR_BASE, ZEPHYR_SDK_INSTALL_DIR and ZEPHYR_TOOLCHAIN_VARIANT are set
      explicitly rather than inherited, so a shell that has not activated the
      venv builds the same thing as one that has.

    The application and MCUboot MUST be built from the same
    pmc-partitions.dtsi. That is why both halves live in one script: a flash
    map disagreement between them produces no build error at all, only a
    corrupt image or an update that is silently ignored.

.PARAMETER Mcuboot
    Build MCUboot as well as the application. Requires mcuboot/pmc-key.conf,
    which is gitignored and names the real signing key.

    ONLY NEEDED WHEN MCUBOOT OR THE FLASH MAP CHANGES, which is rare. The
    bootloader is already on the board and an OTA never replaces it, so
    rebuilding it on every iteration is pure waiting - it roughly doubles the
    turnaround for a one-line application change.

.PARAMETER Pristine
    Delete the build directory first (west build -p always).

.EXAMPLE
    pwsh tools/build.ps1
    pwsh tools/build.ps1 -Pristine -Mcuboot
#>
[CmdletBinding()]
param(
    [switch]$Mcuboot,
    [switch]$Pristine,

    # Extra -D arguments passed straight through to the application build, for
    # trying a Kconfig value WITHOUT editing app/prj.conf. A temporary edit to
    # a committed file is the kind of experiment that gets committed by
    # accident; this leaves no trace in the tree.
    #
    #   tools/build.ps1 -ExtraArgs '-DCONFIG_PMC_OPEN_PA1_ANALOG_SWITCH=n'
    [string[]]$ExtraArgs = @()
)

$ErrorActionPreference = 'Stop'

# Everything is relative to the repository, never to the caller's directory,
# and never to an absolute path under one account - this file is committed.
$repo = Split-Path -Parent $PSScriptRoot
$workspace = Join-Path $HOME 'zephyrproject'
$venvScripts = Join-Path $workspace '.venv\Scripts'
$sdk = Join-Path $HOME 'zephyr-sdk-1.0.1'

foreach ($required in @($workspace, $venvScripts, $sdk)) {
    if (-not (Test-Path $required)) {
        throw "not found: $required"
    }
}

# PATH first: cmake and ninja are in the venv, not on the system PATH.
$env:PATH = "$venvScripts;$env:PATH"
$env:ZEPHYR_BASE = Join-Path $workspace 'zephyr'
$env:ZEPHYR_SDK_INSTALL_DIR = $sdk
$env:ZEPHYR_TOOLCHAIN_VARIANT = 'zephyr'

$board  = 'arduino_portenta_h7/stm32h747xx/m7'
$shield = 'arduino_portenta_machine_control'
$west   = Join-Path $venvScripts 'west.exe'

$pristineArgs = if ($Pristine) { @('-p', 'always') } else { @() }

# --- MCUboot ----------------------------------------------------------------
if ($Mcuboot) {
    $keyConf = Join-Path $repo 'mcuboot\pmc-key.conf'
    if (-not (Test-Path $keyConf)) {
        # Refusing here rather than letting the build fall back is the whole
        # point of the fail-closed placeholder in mcuboot/pmc.conf. MCUboot's
        # ECDSA-P256 default key, root-ec-p256.pem, is COMMITTED in the MCUboot
        # repository and therefore published: a bootloader built with it
        # accepts images anyone can sign. Measured on the Giga, that build
        # succeeds with one warning line among hundreds.
        throw @"
mcuboot/pmc-key.conf is missing, so the signing key path is unknown.

Copy mcuboot/pmc-key.conf.example to mcuboot/pmc-key.conf and point it at the
real key. The key itself lives outside every repository, in
Documents/Arduino/mcuboot-signing/. See mcuboot/README.md.
"@
    }

    $mcubootSrc = Join-Path $workspace 'bootloader\mcuboot\boot\zephyr'
    $confList = @(
        (Join-Path $repo 'mcuboot\pmc.conf')
        $keyConf                      # LAST, so its real key path wins
    ) -join ';'

    Write-Host "==> MCUboot" -ForegroundColor Cyan
    & $west build @pristineArgs -b $board $mcubootSrc -d (Join-Path $repo 'build\mcuboot') -- `
        "-DEXTRA_DTC_OVERLAY_FILE=$(Join-Path $repo 'mcuboot\pmc.overlay')" `
        "-DEXTRA_CONF_FILE=$confList" `
        '-DCONFIG_CONSOLE=n' `
        '-DCONFIG_BOARD_SERIAL_BACKEND_CDC_ACM=n'
    if ($LASTEXITCODE -ne 0) { throw "MCUboot build failed" }
}

# --- Application ------------------------------------------------------------
Write-Host "==> application" -ForegroundColor Cyan
$appArgs = @(
    "-DSHIELD=$shield"
    "-DBOARD_ROOT=$repo"
)

# THE APPLICATION ALWAYS NEEDS ITS SIGNING KEY, AND THIS USED TO BE INSIDE
# `if ($Mcuboot)`.
#
# The two halves need the key for different reasons and the bootloader's reason
# is the rare one: MCUboot is already on the board and an OTA never replaces
# it, so -Mcuboot is hardly ever passed. The APPLICATION needs it on every
# single build, because without it imgtool is never run and the build produces
# only the unsigned zephyr.bin - which MCUboot will not boot and
# tools/ota.ps1 cannot upload.
#
# The failure was silent in the worst possible way. A plain `build.ps1`
# succeeded, printed its size table, and the only hint was one CMake warning
# among hundreds of lines saying the image "will not be bootable by MCUboot
# unless it is signed manually". The next command then failed with
# "missing zephyr.signed.bin - run tools/build.ps1 -Mcuboot first", which
# names the wrong cause and sends you off to rebuild a bootloader that was
# never the problem. The size table even said "Flash zephyr.signed.bin" about
# a file that did not exist.
$appKeyConf = Join-Path $repo 'app\mcuboot-key.conf'
if (-not (Test-Path $appKeyConf)) {
    throw @"
app/mcuboot-key.conf is missing, so no image can be signed and nothing built
here can be installed - over OTA or otherwise.

Copy app/mcuboot-key.conf.example to app/mcuboot-key.conf and point it at the
real key. The key itself lives outside every repository, in
Documents/Arduino/mcuboot-signing/. See mcuboot/README.md.
"@
}
$appArgs += "-DEXTRA_CONF_FILE=$appKeyConf"

if ($ExtraArgs.Count -gt 0) {
    Write-Host "  extra: $($ExtraArgs -join ' ')" -ForegroundColor Yellow
    $appArgs += $ExtraArgs
}

& $west build @pristineArgs -b $board (Join-Path $repo 'app') -d (Join-Path $repo 'build\app') -- @appArgs
if ($LASTEXITCODE -ne 0) { throw "application build failed" }

# --- What was produced ------------------------------------------------------
#
# Printed because the flash map is the thing most worth checking after any
# build, and because `pmc parts` on the running board prints the same numbers
# from the other side. If the two ever disagree, trust neither until you know
# why.
$elf = Join-Path $repo 'build\app\zephyr\zephyr.elf'
$bin = Join-Path $repo 'build\app\zephyr\zephyr.bin'
$signed = Join-Path $repo 'build\app\zephyr\zephyr.signed.bin'

Write-Host ""
foreach ($f in @($bin, $signed)) {
    if (Test-Path $f) {
        $size = (Get-Item $f).Length
        $slot = 0x180000      # keep equal to slot0 in pmc-partitions.dtsi
        $pct = [math]::Round(100.0 * $size / $slot, 2)
        Write-Host ("{0,-22} {1,9:N0} bytes  {2,6}% of slot0" -f (Split-Path -Leaf $f), $size, $pct)
    }
}
# UNCONDITIONAL, AND A FAILURE RATHER THAN A WARNING. It was previously
# `-and $Mcuboot`, which is the one case where it could not fire usefully: an
# unsigned image is a build that cannot be installed, and finding that out from
# the next command is how the cause gets misattributed. Checked after the
# build rather than before it because the only honest test of "was it signed"
# is whether the file is there.
if (-not (Test-Path $signed)) {
    throw @"
the build produced no zephyr.signed.bin, so nothing can be installed.

zephyr.bin alone is not bootable under MCUboot: its vector table has to sit
after MCUboot's 1 KB header, and the image needs a trailer and a signature the
bootloader will accept. Check that CONFIG_BOOTLOADER_MCUBOOT=y in app/prj.conf
and that app/mcuboot-key.conf names a key that exists.
"@
}
Write-Host ""
Write-Host "the plain zephyr.bin is NOT bootable under MCUboot: its vector table" -ForegroundColor DarkGray
Write-Host "has to sit after MCUboot's 1 KB header. Flash zephyr.signed.bin." -ForegroundColor DarkGray
