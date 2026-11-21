<#
.SYNOPSIS
    Watch the two encoder inputs, live, in their own console.

.DESCRIPTION
    The encoder half of the two-console pair - tools/canmon.ps1 is the other.
    It follows /enc.json over the NETWORK, so the serial shell stays free for
    `enc zero` while this window shows what the shaft is doing.

    NOTHING HERE HAS EVER SEEN A SHAFT TURN. Both channels are armed and read
    their lines; at the time of writing no encoder has been connected to this
    board, so every number below has only ever been zero. The window exists so
    that connecting one is the whole of the work.

    WHAT TO LOOK FOR WHEN ONE IS CONNECTED:

      count      should follow the shaft in both directions and come back to
                 the same value after a there-and-back turn
      per rev    the count between the last two index pulses. It should equal
                 4x the encoder's line count, EVERY revolution. A short one
                 means counts were lost, which is the failure a software
                 decoder has and a timer does not - see app/src/enc.h
      illegal    both lines changed between two interrupts. A clean count here
                 is NOT proof nothing was missed; `per rev` is the check

.PARAMETER Ip
    The board's address or mDNS name.

.PARAMETER IntervalMs
    How often to poll.

.EXAMPLE
    pwsh tools/encmon.ps1                  # the mDNS name is the default
#>

[CmdletBinding()]
param(
    # BY NAME, NOT BY ADDRESS - see the note in canmon.ps1 and Get-PmcHost.
    [Alias('Ip')][string]$Target,
    [int]$IntervalMs = 250
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'pmcshell.ps1')
if (-not $Target) { $Target = Get-PmcHost }

Write-Host ""
Write-Host "watching http://$Target/enc.json   (ctrl-c to stop)" -ForegroundColor Cyan
Write-Host ""
Write-Host ("{0,-6} {1,12} {2,10}  {3} {4} {5}  {6,7} {7,9} {8,8}" -f
            'ch', 'count', 'counts/s', 'A', 'B', 'I', 'index', 'per rev',
            'illegal') -ForegroundColor DarkGray

$last = @{}

while ($true) {
    try {
        $j = Invoke-RestMethod -Uri "http://$Target/enc.json" -TimeoutSec 5
    } catch {
        Write-Host ("{0:HH:mm:ss}  board unreachable" -f (Get-Date)) -ForegroundColor DarkYellow
        Start-Sleep -Milliseconds 1000
        continue
    }

    for ($i = 0; $i -lt $j.enc.Count; $i++) {
        $e = $j.enc[$i]
        $name = "ENC$i"

        if (-not $e.ok) {
            if ($last[$name] -ne 'off') {
                Write-Host ("{0,-6} not armed" -f $name) -ForegroundColor DarkYellow
                $last[$name] = 'off'
            }
            continue
        }

        # One line per CHANGE, not per poll: an encoder that is not turning
        # would otherwise fill the window with the same row forever, and the
        # thing worth seeing here is movement.
        $sig = "$($e.count)|$($e.rate)|$($e.a)$($e.b)$($e.i)|$($e.idx)|$($e.per)|$($e.err)"
        if ($last[$name] -eq $sig) { continue }
        $last[$name] = $sig

        $color = if ($e.err -gt 0) { 'Red' } else { 'Gray' }
        Write-Host ("{0,-6} {1,12} {2,10}  {3} {4} {5}  {6,7} {7,9} {8,8}" -f
                    $name, $e.count, $e.rate, $e.a, $e.b, $e.i, $e.idx,
                    $(if ($e.per) { $e.per } else { '-' }), $e.err) `
                   -ForegroundColor $color
    }

    Start-Sleep -Milliseconds $IntervalMs
}
