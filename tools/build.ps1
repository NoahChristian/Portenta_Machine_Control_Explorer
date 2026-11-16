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

.PARAMETER Pristine
    Delete the build directory first (west build -p always).

.EXAMPLE
    pwsh tools/build.ps1
    pwsh tools/build.ps1 -Pristine -Mcuboot
#>
[CmdletBinding()]
param(
    [switch]$Mcuboot,
    [switch]$Pristine
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
if ($Mcuboot) {
    $appArgs += "-DEXTRA_CONF_FILE=$(Join-Path $repo 'app\mcuboot-key.conf')"
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
        $slot = 0x140000      # keep equal to slot0 in pmc-partitions.dtsi
        $pct = [math]::Round(100.0 * $size / $slot, 2)
        Write-Host ("{0,-22} {1,9:N0} bytes  {2,6}% of slot0" -f (Split-Path -Leaf $f), $size, $pct)
    }
}
if (-not (Test-Path $signed) -and $Mcuboot) {
    Write-Warning "no zephyr.signed.bin was produced - check that CONFIG_BOOTLOADER_MCUBOOT=y"
}
Write-Host ""
Write-Host "the plain zephyr.bin is NOT bootable under MCUboot: its vector table" -ForegroundColor DarkGray
Write-Host "has to sit after MCUboot's 1 KB header. Flash zephyr.signed.bin." -ForegroundColor DarkGray
