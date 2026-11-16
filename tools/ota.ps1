<#
.SYNOPSIS
    Update the application over the air, with mcumgr/SMP over UDP.

.DESCRIPTION
    THIS IS THE ORDINARY WAY TO REFLASH THIS BOARD. tools/install_mcuboot.ps1
    and its double-tap RESET are for installing the bootloader and for
    recovery, not for development. The Giga port's reasoning applies here: when
    the update path is also the development path, it gets exercised constantly
    instead of only on the day it matters.

    THE BOARD'S ADDRESS IS NOT STORED ANYWHERE. It is asked for over the USB
    shell, which means there is no IP address in any file in this repository -
    and an address is a property of a DHCP lease, not of the unit, so recording
    one would be recording something that is not true for long. Pass -Ip to
    skip the lookup, or -Serial to point at a particular port.

    WHAT HAPPENS, AND WHY IT IS SAFE BY DEFAULT. `smpmgr upgrade` uploads into
    slot1, marks it for TEST, and resets. MCUboot then swaps slot1 into slot0
    through the scratch area and boots it. If the new image does not call
    boot_write_img_confirmed(), the NEXT reset swaps the old one back. So a bad
    image costs one reboot, not a trip to the bench.

    The confirming is done by app/src/health.c, not by this script and not by
    --confirm, so "the update stuck" means the new image proved its I/O
    answers - not merely that it was uploaded.

.PARAMETER Ip
    The board's address or mDNS name. Discovered over the USB shell if
    omitted. Prefer the name: a DHCP lease is a property of the network, not
    of the unit.

.PARAMETER Serial
    Serial port for discovery. Auto-detected if omitted.

.PARAMETER Confirm
    Mark permanent immediately, skipping the test/revert cycle. Avoid: it
    discards the one property that makes a bad OTA survivable.

.EXAMPLE
    pwsh tools/ota.ps1
    pwsh tools/ota.ps1 -Ip portentaexplorer.local
#>
[CmdletBinding()]
param(
    [string]$Ip,
    [string]$Serial,
    [switch]$Confirm
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$venv = Join-Path $HOME 'zephyrproject\.venv\Scripts'
$smpmgr = Join-Path $venv 'smpmgr.exe'
$img = Join-Path $repo 'build\app\zephyr\zephyr.signed.bin'

if (-not (Test-Path $smpmgr)) { throw "smpmgr not found at $smpmgr" }
if (-not (Test-Path $img)) {
    throw "missing $img - run tools/build.ps1 -Mcuboot first"
}

# The signed image, checked before anything is uploaded. An unsigned image
# uploads perfectly happily and is then rejected by MCUboot at boot, which
# costs a swap cycle to discover.
$magic = ([System.IO.File]::ReadAllBytes($img)[0..3] |
          ForEach-Object { $_.ToString('x2') }) -join ''
if ($magic -ne '3db8f396') {
    throw "$img is not a signed MCUboot image (magic 0x$magic)"
}

function Invoke-ZShell {
    param([string]$Port, [string]$Cmd, [int]$QuietMs = 2500)
    $sp = New-Object System.IO.Ports.SerialPort $Port, 115200, 'None', 8, 'One'
    $sp.ReadTimeout = 500
    try {
        $sp.Open(); $sp.DtrEnable = $true; Start-Sleep -Milliseconds 450
        $sp.DiscardInBuffer(); $sp.Write("$Cmd`r`n")
        $sb = New-Object System.Text.StringBuilder
        $last = [Diagnostics.Stopwatch]::StartNew()
        while ($last.ElapsedMilliseconds -lt $QuietMs) {
            try { $c = $sp.ReadExisting(); if ($c) { [void]$sb.Append($c); $last.Restart() } } catch {}
            Start-Sleep -Milliseconds 60
        }
        return $sb.ToString()
    } finally { if ($sp.IsOpen) { $sp.Close() } }
}

if (-not $Ip) {
    # EVERY candidate port is tried, not the last one.
    #
    # A board that has re-enumerated leaves ghost COM numbers behind, and
    # Windows does not order them usefully - picking one and hoping produced a
    # confident "no IPv4 address found" from a port with nothing on it, which
    # reads exactly like a WiFi problem. Asking each in turn and taking the
    # first that ANSWERS removes the guess.
    $candidates = if ($Serial) { @($Serial) } else {
        [System.IO.Ports.SerialPort]::GetPortNames() | Where-Object { $_ -ne 'COM18' }
    }
    if (-not $candidates) { throw "no serial port to ask; pass -Ip instead" }

    foreach ($c in $candidates) {
        Write-Host "asking $c..." -ForegroundColor DarkGray
        try { $ifc = Invoke-ZShell -Port $c -Cmd 'net iface' -QuietMs 3000 }
        catch { continue }

        # The FIRST address under an interface that is actually up. Matching
        # any dotted quad would also match a netmask, a gateway and a DHCP
        # server, all of which appear in the same block.
        if ($ifc -match '(?m)^\s*((?:\d{1,3}\.){3}\d{1,3})/\d{1,3}\.') {
            $Ip = $Matches[1]
            break
        }
    }

    if (-not $Ip) {
        throw @"
No IPv4 address found on the board.

Check `net iface` over the shell. If wlan0 is down, the board has no stored
credentials or could not join: tools/wifi_provision.ps1 puts them in NVS. Note
the CYW4343W is 802.11 b/g/n, so a 5 GHz-only SSID is never found however
correct the passphrase is.
"@
    }
}

$size = (Get-Item $img).Length
Write-Host ""
Write-Host ("target       {0}" -f $Ip)
Write-Host ("image        {0:N0} bytes" -f $size)
Write-Host ("mode         {0}" -f $(if ($Confirm) { 'CONFIRM - no revert if it misbehaves' } else { 'test - reverts unless the health gate confirms it' }))
Write-Host ""

Write-Host "==> before" -ForegroundColor Cyan
& $smpmgr --ip $Ip --timeout 10 image state-read 2>&1 |
    Select-String -Pattern 'slot=|version=|confirmed=|active=|pending=' |
    ForEach-Object { "  " + $_.Line.Trim() }

Write-Host ""
Write-Host "==> uploading, marking for next boot, resetting" -ForegroundColor Cyan
$args = @('--ip', $Ip, '--timeout', '20', 'upgrade', $img)
if ($Confirm) { $args += '--confirm' }
& $smpmgr @args
if ($LASTEXITCODE -ne 0) { throw "upgrade failed" }

Write-Host ""
Write-Host "==> waiting for the swap and the rejoin" -ForegroundColor Cyan
# The swap moves 1536 KB through a 4 KB-sector QSPI scratch, then the board
# reboots and has to re-associate and take a DHCP lease before it answers
# again. Generous on purpose: declaring failure early here would look exactly
# like a failed swap.
Start-Sleep -Seconds 45

for ($i = 1; $i -le 6; $i++) {
    $out = & $smpmgr --ip $Ip --timeout 10 image state-read 2>&1 | Out-String
    if ($out -match 'version=') {
        Write-Host "==> after" -ForegroundColor Cyan
        $out -split "`n" | Select-String -Pattern 'slot=|version=|confirmed=|active=|pending=' |
            ForEach-Object { "  " + $_.Line.Trim() }
        Write-Host ""
        Write-Host "Check `health` over the shell for whether it confirmed itself," -ForegroundColor DarkGray
        Write-Host "and `mcuboot` for what the bootloader saw. If D1 is still high," -ForegroundColor DarkGray
        Write-Host "a health check is failing and the next reset will revert." -ForegroundColor DarkGray
        exit 0
    }
    Write-Host "  no answer yet (attempt $i of 6)" -ForegroundColor DarkGray
    Start-Sleep -Seconds 15
}

throw @"
The board did not answer after the update.

That is not necessarily a failed swap - it may simply not have rejoined the
network. Check the USB shell: `mcuboot` prints what the bootloader decided and
`net iface` whether it is back on WiFi. If a swap FAILED, run
`swapclear confirm` - one failed swap leaves swap_info set in slot0's trailer
and every later update is silently ignored until it is cleared.
"@
