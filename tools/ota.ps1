<#
.SYNOPSIS
    Update the application over the air, with mcumgr/SMP over UDP.

.DESCRIPTION
    THIS IS THE FALLBACK PATH. Prefer tools/otahttp.ps1, which posts the same
    signed image to POST /ota over TCP and therefore resends what it loses.
    SMP over UDP is lockstep with no window: one datagram lost in either
    direction ends the transfer, because the protocol has nothing to resend it
    with. Measured on this bench on 2026-11-18, loss is about 0.06% per
    datagram - one in 1,600, invisible to a ping test and fatal to a
    1,100-datagram upload roughly half the time. On one occasion five
    consecutive attempts failed. docs/MEASURED.md has the capture.

    IT IS KEPT, AND NOT AS A COURTESY. It drives MCUboot's own image manager
    rather than a route of this application's, so it is the path that still
    works when the panel's HTTP server does not - and it is the only one that
    can erase slot1 or read a slot's state back. tools/install_mcuboot.ps1 and
    its double-tap RESET are for installing the bootloader and for recovery,
    not for either of these.

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

.PARAMETER Resume
    Skip the erase and the upload, on the understanding that slot1 already
    holds this exact image. Checked, not assumed: slot1's hash must equal the
    local image's or the run stops.

.PARAMETER Mtu
    Shrink the SMP frames. Unlikely to help - see the measurement at the call
    site - and left for diagnosis rather than as a remedy.

.EXAMPLE
    pwsh tools/ota.ps1
    pwsh tools/ota.ps1 -Ip <address>       # only to override the name
#>
[CmdletBinding()]
param(
    [string]$Ip,
    [string]$Serial,
    [switch]$Confirm,
    [switch]$Resume,
    [int]$Mtu = 0
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

# Dot-sourced HERE, ABOVE this script's own Invoke-ZShell, and the order is
# load-bearing: PowerShell takes the LAST definition, so the copy further down
# this file keeps winning and nothing about the OTA path changes today. What is
# wanted from pmcshell.ps1 is Get-PmcHost alone.
#
# THE TWO COPIES OF Invoke-ZShell HAVE ALREADY DRIFTED, which is the thing that
# file was created to prevent: this one bounds the read with a computed
# $TotalMs of four quiet windows, floor 8 s; the shared one takes a flat
# $MaxMs = 20000. Both terminate, so the live-lock lesson did reach both - but
# they are two implementations of one rule and only one of them is the one with
# the reasoning written down. Unifying them means re-testing the OTA path,
# which is a change of its own; it is on the punchlist rather than smuggled in
# here.
. (Join-Path $PSScriptRoot 'pmcshell.ps1')

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
$imgBytes = [System.IO.File]::ReadAllBytes($img)
$magic = ($imgBytes[0..3] | ForEach-Object { $_.ToString('x2') }) -join ''
if ($magic -ne '3db8f396') {
    throw "$img is not a signed MCUboot image (magic 0x$magic)"
}

# THE IMAGE'S OWN HASH, COMPUTED HERE, SO "DID THE BYTES ARRIVE?" HAS AN ANSWER.
#
# bootutil_img_hash() digests the header (ih_hdr_size bytes), then ih_img_size
# bytes of payload, then ih_protect_tlv_size bytes of protected TLVs - and
# stops. The unprotected TLV area at the end, which carries the signature and
# the digest itself, is NOT part of it. So this is reproducible from the file,
# and it is the same value `image state-read` reports for whichever slot holds
# the image.
#
# Hashing the whole file instead is the obvious mistake and it never matches:
# for the 0.94.0 image the correct digest is F47DE115... over 1,130,568 bytes
# and the whole-file digest is 230ADB9C... over 1,130,720. A comparison that
# can never succeed is worse than no comparison, because it reports a failed
# transfer for a good one.
#
# Checked against the image's own SHA256 TLV rather than against the board:
# `imgtool dumpinfo` prints that TLV, and it agrees to the byte. Which means
# this function was verified without a network, a board, or a flash cycle.
function Get-McubootImageHash {
    param([byte[]]$Bytes)
    $hdrSize = [int][BitConverter]::ToUInt16($Bytes, 8)
    $protTlv = [int][BitConverter]::ToUInt16($Bytes, 10)
    $imgSize = [int][BitConverter]::ToUInt32($Bytes, 12)
    $n = $hdrSize + $imgSize + $protTlv
    if ($n -le 0 -or $n -gt $Bytes.Length) {
        throw "image header is not consistent with the file: it claims $n bytes of a $($Bytes.Length)-byte file"
    }
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        return (($sha.ComputeHash($Bytes, 0, $n)) |
                ForEach-Object { $_.ToString('X2') }) -join ''
    } finally { $sha.Dispose() }
}

$expectHash = Get-McubootImageHash $imgBytes
$expectVer = "{0}.{1}.{2}" -f $imgBytes[20], $imgBytes[21],
                              [BitConverter]::ToUInt16($imgBytes, 22)

# The 64-hex run inside a slot's block. The hash is printed as a multi-line
# repr:
#
#     hash=HashBytes(
#         'B7FB...'
#     ),
#
# so a field matcher that stops at the end of the line finds "HashBytes("
# rather than the digest. Nothing else in a slot block is 64 hex characters
# long, which makes the run unambiguous.
function Get-SlotHash {
    param([string]$Raw, [int]$Slot = 1)
    $block = ($Raw -split 'slot=' |
              Where-Object { $_ -match "^\s*$Slot\s*," } | Select-Object -First 1)
    if (-not $block) { return $null }
    if ($block -match '([0-9A-Fa-f]{64})') { return $Matches[1].ToUpper() }
    return $null
}

# Send a shell command and collect the reply.
#
# TWO DEADLINES, AND THE SECOND ONE IS LOAD BEARING.
#
#   QuietMs   stop once the console has been silent this long. This is how a
#             reply is known to be complete, since the shell marks no end.
#   TotalMs   stop regardless. Without it, a console that keeps talking holds
#             this function forever.
#
# THAT IS NOT HYPOTHETICAL AND IT COST A SESSION. With an RTD front end
# unattached, this board logs
#
#     <err> MAX31865: Fault Register: 0x04, Over/under voltage fault
#
# continuously. Measured on the bench with DTR asserted: 3129 bytes in 6 s,
# LONGEST SILENCE 599 ms. Every QuietMs this script passes -- 1800, 2500,
# 3000 -- is longer than that, so `$last.Restart()` fired before the quiet
# window could ever close and the loop never exited. `tools/ota.ps1` sat for
# over ten minutes having printed nothing, on a board that was answering its
# console and serving its web panel the whole time.
#
# A faulting sensor is an ORDINARY state for a bench board. A discovery routine
# that cannot survive one is a discovery routine that works until the day you
# need it.
#
# BEWARE MEASURING THIS WITHOUT DTR. Zephyr's CDC-ACM sends nothing until DTR
# is asserted, so a probe that omits it reports the console silent and appears
# to exonerate exactly this cause. That misreading is why the fix took two
# attempts.
#
# A capped read still works: the caller matches on CONTENT -- the mdns name, a
# dotted quad -- and a reply interleaved with log lines still contains it. What
# a cap cannot do is turn silence into an answer, so it says when it fired
# rather than returning a short read that looks like a complete one.
function Invoke-ZShell {
    param(
        [string]$Port,
        [string]$Cmd,
        [int]$QuietMs = 2500,
        # Four quiet windows, floor 8 s: long enough that a merely slow reply
        # still completes normally, short enough that a stuck port is a pause
        # rather than a hang.
        [int]$TotalMs = 0
    )
    if ($TotalMs -le 0) { $TotalMs = [Math]::Max(8000, $QuietMs * 4) }

    $sp = New-Object System.IO.Ports.SerialPort $Port, 115200, 'None', 8, 'One'
    $sp.ReadTimeout = 500
    try {
        $sp.Open(); $sp.DtrEnable = $true; Start-Sleep -Milliseconds 450
        $sp.DiscardInBuffer(); $sp.Write("$Cmd`r`n")
        $sb = New-Object System.Text.StringBuilder
        $last = [Diagnostics.Stopwatch]::StartNew()
        $total = [Diagnostics.Stopwatch]::StartNew()
        while ($last.ElapsedMilliseconds -lt $QuietMs -and
               $total.ElapsedMilliseconds -lt $TotalMs) {
            try { $c = $sp.ReadExisting(); if ($c) { [void]$sb.Append($c); $last.Restart() } } catch {}
            Start-Sleep -Milliseconds 60
        }
        if ($total.ElapsedMilliseconds -ge $TotalMs) {
            # Said out loud. A cap that fires silently is the same class of
            # problem as the hang it replaced: the operator cannot tell a
            # complete reply from a truncated one.
            Write-Host ("  {0} kept talking for {1} ms; reading what arrived" -f $Port, $TotalMs) `
                -ForegroundColor DarkYellow
        }
        return $sb.ToString()
    } finally { if ($sp.IsOpen) { $sp.Close() } }
}

if (-not $Ip) {
    # THE NAME FIRST, because it costs one UDP exchange and the serial sweep
    # below costs several seconds and a port that may be in use.
    #
    # It is a PROBE and not a substitution, which is the whole point. The
    # mDNS name resolves to ONE address - whichever interface the responder
    # answers on - and this board holds an address on both eth0 and wlan0.
    # The sweep below exists because the right address is the one whose SMP
    # SERVER ANSWERS, not the first one found, and a name cannot promise that.
    # So: try the name, keep it only if SMP replies there, otherwise fall
    # through to the sweep that checks each address in turn.
    $mdns = Get-PmcHost -Repo $repo
    Write-Host "trying SMP at $mdns ..." -ForegroundColor DarkGray
    $probe = & $smpmgr --ip $mdns --timeout 6 image state-read 2>&1 | Out-String
    if ($probe -match 'ImageState|slot=') {
        $Ip = $mdns
        Write-Host "  SMP answers at $mdns" -ForegroundColor DarkGray
    } else {
        Write-Host "  no SMP reply by name - asking the board over USB" `
            -ForegroundColor DarkGray
    }
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

        # EVERY address, then the one whose SMP SERVER ANSWERS -- not the first
        # one that parses.
        #
        # Matching a dotted quad followed by `/` and a netmask is still how an
        # address is told apart from the netmask, gateway and DHCP server that
        # share the block. What changed is which of the matches is believed.
        #
        # This board has eth0 AND wlan0, both up, both holding a DHCP address on
        # the same subnet. Measured on the bench:
        #
        #     eth0    ping=True   smp=no SMP reply
        #     wlan0   ping=True   smp=ANSWERS
        #
        # Taking the first match took eth0 -- the one that pings and cannot be
        # updated. The old comment called it "the FIRST address under an
        # interface that is actually up", which was true and not sufficient:
        # both interfaces are up, and being up is not the same as being the one
        # the SMP server is bound to. Discovery would hand back a reachable,
        # useless address, and the failure would surface as an upload timeout
        # against a board that was answering its console throughout.
        #
        # `image state-read` is the probe because it is READ-ONLY: it stages
        # nothing, erases nothing, and is the same call used to read the slots
        # later. Asking the question that matters beats inferring it from
        # interface order, which is not a promise of anything.
        $addrs = [regex]::Matches($ifc, '(?m)^\s*((?:\d{1,3}\.){3}\d{1,3})/\d{1,3}\.') |
                 ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique
        if (-not $addrs) { continue }

        foreach ($a in $addrs) {
            Write-Host "  trying SMP at $a ..." -ForegroundColor DarkGray
            $probe = & $smpmgr --ip $a --timeout 6 image state-read 2>&1 | Out-String
            if ($probe -match 'ImageState|slot=') {
                $Ip = $a
                Write-Host "  SMP answers at $a" -ForegroundColor DarkGray
                break
            }
            Write-Host "  no SMP reply at $a" -ForegroundColor DarkGray
        }
        if ($Ip) { break }

        # Addresses but no SMP on any of them is a DIFFERENT fault from having
        # no address, and saying so saves looking at WiFi credentials that are
        # fine.
        Write-Host ("  {0} has {1} address(es) but none answered SMP" -f $c, $addrs.Count) `
            -ForegroundColor DarkYellow
    }

    if (-not $Ip) {
        throw @"
No address on this board answered SMP.

Two different faults end up here, and the lines above say which:

  "no IPv4 address"      the board has no address at all. If wlan0 is down it
                         has no stored credentials or could not join:
                         tools/wifi_provision.ps1 puts them in NVS. Note the
                         CYW4343W is 802.11 b/g/n, so a 5 GHz-only SSID is
                         never found however correct the passphrase is.

  "none answered SMP"    the board HAS an address and is reachable, but the
                         SMP server is not answering there. It may be bound to
                         a different interface -- this board can hold an
                         address on both eth0 and wlan0 -- or MCUMGR may not be
                         in the running image at all. Pass -Ip to aim at a
                         specific one, and `net iface` over the shell lists
                         them.
"@
    }
}

$size = $imgBytes.Length
Write-Host ""
Write-Host ("target       {0}" -f $Ip)
Write-Host ("image        {0}, {1:N0} bytes" -f $expectVer, $size)
Write-Host ("hash         {0}..." -f $expectHash.Substring(0, 16))
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
    # IT NO LONGER ASSUMES. It used to say "slot1 is assumed already staged"
    # and go straight to marking it pending, which is a bad thing to assume on
    # exactly the path that exists because an upload ended ambiguously: the
    # reason to reach for -Resume is not knowing what is in slot1. Marked
    # pending wrongly, the board resets into a half-written image, MCUboot
    # refuses it and reverts - survivable, but it reports as a mystery.
    #
    # So ask the board, and compare the hash rather than the version. A slot
    # can report a plausible VERSION while only part way written, because the
    # MCUboot header lands in the first kilobyte; the hash covers every byte.
    Write-Host "==> resuming: checking slot1 holds this exact image" -ForegroundColor Yellow
    $resumeHash = Get-SlotHash (Get-State) 1
    if (-not $resumeHash) {
        throw @"
-Resume was asked for, but slot1 holds no readable image.

Nothing is staged, so there is nothing to resume. Run again without -Resume.
"@
    }
    if ($resumeHash -ne $expectHash) {
        throw @"
-Resume was asked for, but slot1 holds a DIFFERENT image.

    slot1  $resumeHash
    local  $expectHash

That is either an older staged image or a partial write of this one. Either
way the bytes for this image are not all there, so run again without -Resume.
"@
    }
    Write-Host "  slot1's hash matches the local image" -ForegroundColor Green
} else {

Write-Host "==> 1/5 erasing slot1" -ForegroundColor Cyan
& $smpmgr --ip $Ip --timeout 150 image erase 1
if ($LASTEXITCODE -ne 0) {
    # A NON-ZERO EXIT HERE DOES NOT MEAN THE ERASE FAILED.
    #
    # The same lesson as the upload below, and it was learned twice because
    # only the upload got the fix the first time. Measured 2026-11-17: this
    # step exited 1 with "Timeout (150.0s) waiting for request", and slot1
    # was empty when the board was asked - the erase had run to completion.
    #
    # Two things make it look like a failure. Erasing 1280 kB of QSPI in
    # 4 kB sectors takes a long time, and while the flash driver holds the
    # mcumgr workqueue nothing else on the SMP transport is answered either -
    # so even image state-read times out, which reads as a dead board. It is
    # not: the panel keeps serving on its own thread throughout. And on a
    # lossy link the erase RESPONSE can simply be dropped, in a protocol with
    # no retransmission, leaving the client waiting for a reply that was sent.
    #
    # So ask the board. It is the only authority on whether slot1 is empty,
    # and the generous timeout here is deliberate - it is waiting out an
    # erase that may still be in progress, not polling a reboot.
    Write-Host "    smpmgr exited non-zero - asking the board whether slot1" -ForegroundColor Yellow
    Write-Host "    is actually empty before calling this a failure" -ForegroundColor Yellow
    $afterErase = Get-State -TimeoutSec 180
    if ($afterErase -notmatch 'slot=') {
        throw "erase failed - the board did not answer at all, so slot1's state is unknown"
    }
    if (Get-SlotHash $afterErase 1) {
        # Asked by hash rather than by `-match 'slot=1'`, which is the literal
        # text of a block label and not a statement about the slot's contents.
        throw "erase failed - slot1 still holds a readable image"
    }
    Write-Host "    slot1 is empty: the erase completed. Continuing." -ForegroundColor Green
}

Write-Host ""
Write-Host "==> 2/5 uploading to slot1" -ForegroundColor Cyan

# -Mtu MAKES THINGS WORSE, AND THE REASON IS WORTH THE PARAGRAPH.
#
# This comment used to claim loss rose with frame size - 0% at 32 bytes, 20% at
# 500, 73% at 1000 - and recommend a smaller MTU on that basis. Both the
# numbers and the conclusion are withdrawn. They came from `ping -w 1000`,
# which counts a merely LATE reply as lost; larger frames take longer, so the
# measurement manufactured a loss-versus-size curve out of its own timeout.
#
# What the link actually does is lose about 0.06% of datagrams REGARDLESS of
# size - one in 1,600, established by arithmetic rather than by pinging:
# roughly 1,100 datagrams per image and about half of attempts succeeding gives
# (1 - p) ** 1100 ~= 0.5. Under per-datagram loss a smaller MTU multiplies the
# number of lockstep exchanges, so four times the datagrams is four times the
# chance of losing one. Measured: an --mtu 256 attempt failed where the default
# had succeeded minutes earlier.
#
# So this is left for diagnosis - changing the frame count is a way to test a
# per-datagram model against a per-byte one - and not as a remedy. The remedy
# is a transport that resends what it loses, which is tools/otahttp.ps1.
$upArgs = @('--ip', $Ip, '--timeout', '90')
if ($Mtu -gt 0) {
    Write-Host "    --mtu $Mtu (frames shrunk deliberately)" -ForegroundColor Yellow
    $upArgs += @('--mtu', "$Mtu")
}
& $smpmgr @upArgs image upload --slot 1 $img
if ($LASTEXITCODE -ne 0) {
    # A NON-ZERO EXIT HERE DOES NOT MEAN THE TRANSFER FAILED.
    #
    # smpmgr can report an error AFTER the upload has reached 100% and the
    # image is correctly staged - the comment on -Resume below has always said
    # so. What was missing is that this line then threw "upload failed", and a
    # reader with no console believed it.
    #
    # That cost most of an evening. Four explanations for a "broken" OTA were
    # proposed and tested - processor load, WiFi power save, an RS485
    # interrupt storm, the panel's own polling - and several readings behind
    # them were not about the board at all. They were this exit code.
    #
    # So ask the board before declaring anything - BY HASH.
    #
    # The check here used to be `$after -match "0\.\d+\.\d+"` and
    # `$after -match "slot=1"`, which is not a check. The first matches any
    # 0.x.y anywhere in the output, including slot0's own version - so it
    # passes on a board that received nothing - and the second matches the
    # literal text "slot=1", which `image state-read` prints whether or not
    # slot1 holds anything. It would have called the transfer complete with a
    # previous image still staged, then marked THAT pending and reset into it.
    #
    # The hash is the only thing that answers the question actually being
    # asked. A slot can report a plausible VERSION while only part way
    # written, because the MCUboot header lands in the first kilobyte.
    Write-Host ""
    Write-Host "    smpmgr exited non-zero - asking the board whether the" -ForegroundColor Yellow
    Write-Host "    image actually arrived before calling this a failure" -ForegroundColor Yellow
    $gotHash = Get-SlotHash (Get-State) 1
    if ($gotHash -eq $expectHash) {
        Write-Host ""
        Write-Host "    THE TRANSFER COMPLETED. slot1's hash matches the local" -ForegroundColor Green
        Write-Host "    image; smpmgr reported an error after the fact." -ForegroundColor Green
        Write-Host ""
    } elseif (-not $gotHash) {
        throw "upload failed - slot1 holds no readable image, so this one is real"
    } else {
        throw @"
upload failed - slot1 holds a different image, so this one is real.

    slot1  $gotHash
    local  $expectHash

A partial write reports a hash too, over whatever is in the slot. Run again.
"@
    }
}

Write-Host ""
}

Write-Host "==> 3/5 reading back the staged image's hash" -ForegroundColor Cyan
$staged = Get-State
$hash = Get-SlotHash $staged 1
if (-not $hash) { throw "slot1 holds no readable image - nothing was staged" }

# THE BOARD'S HASH AGAINST THE LOCAL ONE, ON THE SUCCESS PATH TOO.
#
# The failure path above checks this, but a clean exit from smpmgr is not
# evidence either: what gets marked pending and booted is whatever is in
# slot1, so the comparison belongs here rather than only where something has
# already gone visibly wrong.
if ($hash -ne $expectHash) {
    throw @"
slot1 does not hold the image that was just uploaded.

    slot1  $hash
    local  $expectHash

smpmgr exited cleanly, so this is the case worth stopping for: nothing has
been marked pending yet and the board is still running a good image.
"@
}
$stagedVer = Get-SlotField $staged 1 'version'
Write-Host ("  slot1 is now {0}, hash {1}... - matches the local image" -f `
    $stagedVer, $hash.Substring(0, 16))

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
