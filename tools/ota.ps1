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
    [switch]$Confirm,
    [switch]$Resume
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

        # IDENTIFY BEFORE TRUSTING. Another Zephyr board on this bench answers
        # `net iface` perfectly well and would hand over ITS address, which is
        # how an update ends up aimed at the wrong unit. A board that does not
        # reply "portentaexplorer" is skipped.
        try {
            $who = Invoke-ZShell -Port $c -Cmd 'mdnsname' -QuietMs 1800
            if ($who -notmatch 'portentaexplorer') {
                Write-Host "  not this project's firmware; skipping" -ForegroundColor DarkGray
                continue
            }
            $ifc = Invoke-ZShell -Port $c -Cmd 'net iface' -QuietMs 3000
        } catch { continue }

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

function Get-State {
    param([int]$TimeoutSec = 20)
    # A short timeout in the poll loop: a board that is still rebooting will
    # not answer no matter how long one waits, and waiting 20 s per attempt
    # turns a handful of retries into minutes.
    $raw = & $smpmgr --ip $Ip --timeout $TimeoutSec image state-read 2>&1 | Out-String
    return $raw
}

function Get-SlotField {
    param([string]$Raw, [int]$Slot, [string]$Field)
    # The slot blocks come in order; take the text after "slot=N," up to the
    # next "slot=" or the end.
    $parts = $Raw -split 'slot='
    foreach ($p in $parts) {
        if ($p -match "^\s*$Slot\s*,") {
            if ($p -match "$Field\s*=\s*'?([^',\r\n\)]+)") { return $Matches[1].Trim() }
        }
    }
    return $null
}

Write-Host "==> before" -ForegroundColor Cyan
$before = Get-State
if ($before -notmatch 'slot=') {
    throw @"
The board did not answer a state-read.

Note that smpmgr prints "Connecting ... OK" for UDP even when nothing is
there - it is a local socket, not a reply. Check `net iface` over the shell:
an interface that is up with no IPv4 address looks identical from here to a
board that is switched off.
"@
}
Write-Host ("  slot0 {0}   slot1 {1}" -f `
    (Get-SlotField $before 0 'version'), (Get-SlotField $before 1 'version'))

# --- EACH STEP EXPLICITLY, AND VERIFIED ------------------------------------
#
# NOT `smpmgr upgrade`. That composite uploads, marks pending and resets in one
# call, and its mark DID NOT TAKE here: the upload reported 100%, the reset was
# acknowledged, the board came back on the OLD image, and slot1 read
# pending=False. Nothing reported an error at any point. So each step is done
# separately and the result is read back, because the whole value of an update
# mechanism is knowing whether it worked.
# ERASE FIRST, AS ITS OWN STEP.
#
# With CONFIG_IMG_ERASE_PROGRESSIVELY the image manager erases ahead while
# writing, and when slot1 already holds an image that erase lands inside a
# write request. Measured: uploading over a populated slot timed out at 20 s
# with "Connection to device lost", while erasing separately took 7.5 s and the
# subsequent upload took 18 s at 56 kB/s. Same bytes, same link - the only
# difference is which request had to wait for 1536 KB of QSPI to be erased.
Write-Host ""
if ($Resume) {
    # RESUME: the bytes are already in slot1, so do not erase and re-send them.
    #
    # This exists because the upload and the tool that drives it fail
    # independently. smpmgr has timed out "waiting for request" AFTER the
    # transfer reached 100% - the image was staged correctly and the wrapper
    # reported failure - and the reflex of running the whole thing again
    # erases a good image and re-sends a megabyte over WiFi at 64 kB/s to
    # reach the state it was already in.
    #
    # Verify with `image state-read` before reaching for this: slot1 should
    # already hold the version being shipped. If it does not, a plain run is
    # the right thing.
    Write-Host "==> resuming: slot1 is assumed already staged" -ForegroundColor Yellow
} else {

Write-Host "==> 1/5 erasing slot1" -ForegroundColor Cyan
& $smpmgr --ip $Ip --timeout 150 image erase 1
if ($LASTEXITCODE -ne 0) { throw "erase failed" }

Write-Host ""
Write-Host "==> 2/5 uploading to slot1" -ForegroundColor Cyan
& $smpmgr --ip $Ip --timeout 90 image upload --slot 1 $img
if ($LASTEXITCODE -ne 0) { throw "upload failed" }

Write-Host ""
}

Write-Host "==> 3/5 reading back the staged image's hash" -ForegroundColor Cyan
$staged = Get-State
# The hash is printed as a multi-line repr:
#
#     hash=HashBytes(
#         'B7FB...'
#     ),
#
# so a field matcher that stops at the end of the line finds "HashBytes("
# rather than the digest. Take the 64-hex run from inside slot 1's block
# instead, which is unambiguous because nothing else in the block is 64 hex
# characters long.
$slot1Block = ($staged -split 'slot=' | Where-Object { $_ -match '^\s*1\s*,' } | Select-Object -First 1)
if (-not $slot1Block) { throw "slot1 does not appear in the state - nothing was staged" }
if ($slot1Block -match "([0-9A-Fa-f]{64})") { $hash = $Matches[1] } else { throw "could not read slot1's hash" }
$stagedVer = Get-SlotField $staged 1 'version'
Write-Host ("  slot1 is now {0}, hash {1}..." -f $stagedVer, $hash.Substring(0,16))

Write-Host ""
Write-Host "==> 4/5 marking it for test, and CHECKING the mark took" -ForegroundColor Cyan
& $smpmgr --ip $Ip --timeout 20 image state-write $hash | Out-Null
$marked = Get-State
if ((Get-SlotField $marked 1 'pending') -ne 'True') {
    throw @"
slot1 is staged but NOT pending, so a reset would boot the old image.

This is the exact failure that made `smpmgr upgrade` unusable here: everything
reports success and nothing has been marked. Do not reset - the board is still
running a good image. Re-run, or mark it by hand with

    smpmgr --ip $Ip image state-write $hash
"@
}
Write-Host "  pending=True"

Write-Host ""
Write-Host "==> 5/5 resetting" -ForegroundColor Cyan
& $smpmgr --ip $Ip --timeout 20 os reset | Out-Null

Write-Host ""
Write-Host "==> waiting for the swap and the rejoin" -ForegroundColor Cyan
# POLL, DO NOT SLEEP BLIND. The swap itself takes seconds; what varies is the
# re-association and the DHCP lease. A fixed wait long enough for the worst
# case makes every GOOD run pay the worst case too, which is how a 20-second
# operation turned into minutes.
Start-Sleep -Seconds 12

for ($i = 1; $i -le 10; $i++) {
    $after = Get-State -TimeoutSec 5
    if ($after -match 'slot=') {
        $v0 = Get-SlotField $after 0 'version'
        Write-Host "==> after" -ForegroundColor Cyan
        Write-Host ("  slot0 {0}   slot1 {1}" -f $v0, (Get-SlotField $after 1 'version'))
        Write-Host ("  confirmed {0}" -f (Get-SlotField $after 0 'confirmed'))
        Write-Host ""
        if ($v0 -eq $stagedVer) {
            Write-Host "the staged image is running." -ForegroundColor Green
        } else {
            Write-Warning "slot0 is $v0, expected $stagedVer - the swap did not take. Run `mcuboot` over the shell."
        }
        Write-Host "It confirms itself once every health check passes; until then it" -ForegroundColor DarkGray
        Write-Host "reverts on the next reset and D1 stays high. `health` says which." -ForegroundColor DarkGray
        exit 0
    }
    Write-Host "  no answer yet (attempt $i of 10)" -ForegroundColor DarkGray
    Start-Sleep -Seconds 6
}

throw @"
The board did not answer after the update.

That is not necessarily a failed swap - it may not have rejoined the network.
Over the USB shell, `mcuboot` prints what the bootloader decided and
`net iface` whether it is back on WiFi. If a swap FAILED, `swapclear confirm`
is the way back: one failed swap leaves swap_info set in slot0's trailer and
every later update is silently ignored until it is cleared.
"@
