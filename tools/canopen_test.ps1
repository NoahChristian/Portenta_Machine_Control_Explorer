<#
.SYNOPSIS
    Run the board's CANopen self-test over the USB shell and ASSERT the result.

.DESCRIPTION
    THE ASSERTION IS THE POINT. The board already prints PASS and FAIL per
    check; a wrapper that printed the same text and exited 0 either way would
    be a transcript, not a test. This one throws unless the board reports every
    check passed, so it can be run from a build step or by hand and the exit
    code means something.

    WHAT IT PROVES. The node runs against itself through INTERNAL loopback:
    the NMT decode, all four state transitions, the heartbeat cadence, the
    object dictionary, the expedited SDO server in both directions and the
    abort path. It touches nothing past the MCU - no transceiver, no wire, no
    second node - and the board's own output says so in its header. The
    receive path off a real wire is in the TBD list in docs/MEASURED.md.

    WHY THE DEADLINE IS LARGE. The self-test takes a few seconds by design
    (three heartbeats at 200 ms, four state transitions, five SDO exchanges),
    and the board logs an open-RTD fault about ten times a second with no
    probe connected - faster than any quiet window. So the ceiling is what
    ends the read, not the silence. `pmcshell.ps1` has that lesson written
    into it; this just has to pass a number big enough.

.PARAMETER Port
    Serial port of the Zephyr shell. Auto-detected if omitted.

.EXAMPLE
    pwsh tools/canopen_test.ps1
#>
[CmdletBinding()]
param(
    [string]$Port
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'pmcshell.ps1')

$Port = Find-PmcPort -Preferred $Port
if (-not $Port) {
    throw @"
No port answered as this project's firmware.

Every serial port was asked ``mdnsname`` and none replied "portentaexplorer".
Either the board is not running this firmware, or it is in the bootloader.
"@
}

Write-Host ""
Write-Host "port         $Port"
Write-Host ""

Write-Host "==> canopen status" -ForegroundColor Cyan
$before = Invoke-ZShell -Port $Port -Cmd 'canopen status' -QuietMs 1500 -MaxMs 8000
$before -split "`r?`n" | Where-Object { $_ -notmatch 'MAX31865' } | Write-Host

Write-Host "==> canopen selftest" -ForegroundColor Cyan
$out = Invoke-ZShell -Port $Port -Cmd 'canopen selftest' -QuietMs 2500 -MaxMs 40000

# The board's log lines are interleaved with the test's output, so filter the
# noise rather than expecting a clean stream. The fault spam is a separate,
# pre-existing condition - an open RTD input - and is not this test's business.
$lines = $out -split "`r?`n" | Where-Object { $_ -notmatch 'MAX31865' }
$lines | Write-Host

$fails = @($lines | Where-Object { $_ -match '\bFAIL\b' })
$passes = @($lines | Where-Object { $_ -match '\bPASS\b' })
$summary = @($lines | Where-Object { $_ -match '(\d+) passed' })

Write-Host ""
if ($fails.Count -gt 0) {
    throw "$($fails.Count) check(s) FAILED:`n$($fails -join "`n")"
}
if ($summary.Count -eq 0) {
    # A SILENT RUN IS A FAILURE, NOT A PASS. No summary line means the read
    # ended before the board finished - a bigger -MaxMs, or a board that is
    # not running this command at all. Reporting "no failures" here would be
    # the false clean this repository's tooling exists to avoid.
    throw @"
The board printed no summary line, so nothing was proven.

Expected a line reading "<n> passed" from ``canopen selftest``. Either the
read ended early - raise -MaxMs - or this image has CONFIG_PMC_CANOPEN off,
in which case the command does not exist and the shell said so above.
"@
}
if ($passes.Count -lt 10) {
    throw "only $($passes.Count) of the 10 checks reported PASS"
}

Write-Host "all $($passes.Count) checks passed on $Port" -ForegroundColor Green
Write-Host "Internal loopback only: no transceiver, no wire, no second node." `
    -ForegroundColor DarkGray
