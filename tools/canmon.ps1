<#
.SYNOPSIS
    Watch the board's CAN receive window, live, in its own console.

.DESCRIPTION
    THE POINT OF THIS SCRIPT IS THE SECOND CONSOLE. Testing a bus needs one
    thing making traffic and another watching it, and on this bench both ends
    were the same serial shell - so sending and watching took turns and
    nothing could be observed while it happened.

    This follows /can.json over the NETWORK, not the serial port, which is
    what makes the pair work: the shell on COM* stays free for `can send` and
    `can rep` while this window prints what arrives.

    IT USES A CURSOR AND REPORTS WHAT IT MISSES. /can.json takes ?from=<n>
    and returns the frames after that index plus the next cursor. When the
    board's ring has wrapped past the cursor it says so, and this prints a
    gap line rather than resuming quietly - a monitor that hides an overrun
    shows a bus that looks calmer than it is.

    WHAT YOU CAN ACTUALLY TEST WITH ONE BOARD:

      loopback     frames come back internally. Measured: the TX pin stays
                   recessive, so nothing reaches the transceiver. This tests
                   the controller, the filter, the ring and this script.

      normal       with no second node nothing acknowledges a frame, so the
                   controller parks at ERROR_PASSIVE and retransmits forever.
                   Nothing is received - but CANH/CANL carry continuous
                   traffic, so this is the setup for metering the transceiver.

      two nodes    the only way to see a frame actually received off a wire.

.PARAMETER Ip
    The board's address or mDNS name.

.PARAMETER IntervalMs
    How often to poll. 250 ms keeps a 32-frame ring from wrapping at the
    repeat rates this is used with.

.EXAMPLE
    # console 1 - watch
    pwsh tools/canmon.ps1                  # the mDNS name is the default

    # console 2 - make traffic, over the serial shell
    can loopback
    can rep 200 0x123 de ad be ef
#>

[CmdletBinding()]
param(
    # BY NAME, NOT BY ADDRESS. The default comes from Get-PmcHost, which reads
    # CONFIG_NET_HOSTNAME out of the firmware's own prj.conf. An address is a
    # DHCP lease and this board changes subnet when it switches between
    # Ethernet and the radio; the name follows it. -Ip still works, so an
    # invocation written down earlier does not break.
    [Alias('Ip')][string]$Target,
    [int]$IntervalMs = 250
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'pmcshell.ps1')
if (-not $Target) { $Target = Get-PmcHost }
$cursor = 0
$started = Get-Date
$seen = 0
$states = @('ERROR_ACTIVE', 'ERROR_WARNING', 'ERROR_PASSIVE', 'BUS_OFF',
            'STOPPED')

Write-Host ""
Write-Host "watching http://$Target/can.json   (ctrl-c to stop)" -ForegroundColor Cyan
Write-Host ""

# Start at the END of what the board already has, not the beginning: this is
# a monitor, and replaying history on startup would look like live traffic.
try {
    $first = Invoke-RestMethod -Uri "http://$Target/can.json" -TimeoutSec 5
    $cursor = $first.next
} catch {
    Write-Host "cannot reach the board: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

$lastLine = ''

while ($true) {
    try {
        $j = Invoke-RestMethod -Uri "http://$Target/can.json?from=$cursor" -TimeoutSec 5
    } catch {
        Write-Host ("{0:HH:mm:ss}  board unreachable" -f (Get-Date)) -ForegroundColor DarkYellow
        Start-Sleep -Milliseconds 1000
        continue
    }

    if ($j.lost) {
        Write-Host ("{0:HH:mm:ss}  --- frames were dropped: the ring wrapped " +
                    "before this window read it ---" -f (Get-Date)) -ForegroundColor DarkYellow
    }

    foreach ($f in $j.f) {
        $seen++
        $hex = ($f.d -split '(..)' | Where-Object { $_ }) -join ' '
        $id = '0x{0:x}' -f $f.id
        if ($f.ext) { $id = 'x' + $id }
        Write-Host ("{0,10:N3}s  {1,-11} {2}  {3}{4}" -f ($f.t / 1000.0), $id,
                    $f.dlc, $hex, $(if ($f.rtr) { '  RTR' } else { '' }))
    }
    $cursor = $j.next

    # One status line, rewritten rather than appended, so the frames above
    # stay readable. It carries the two numbers that explain a silent bus:
    # the controller's state and whether the TX pin is moving at all.
    $st = $states[$j.state]
    $line = ("  [{0}  {1}  {2} bit/s  txErr {3}  rx {4}  txPin {5}{6}]" -f
             $st,
             $(if ($j.loopback) { 'loopback' } else { 'normal' }),
             $j.bitrate, $j.txErr, $j.rx,
             $(if ($j.txPin) { 'moving' } else { 'idle' }),
             $(if ($j.repMs) { "  repeating every $($j.repMs) ms, $($j.repSent) sent" } else { '' }))
    if ($line -ne $lastLine) {
        $color = if ($j.state -eq 0) { 'DarkGray' } else { 'DarkYellow' }
        Write-Host $line -ForegroundColor $color
        $lastLine = $line
    }

    Start-Sleep -Milliseconds $IntervalMs
}
