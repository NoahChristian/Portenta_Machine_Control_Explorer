<#
.SYNOPSIS
    Push WiFi credentials from app/app_secrets.h into the board's NVS, once.

.DESCRIPTION
    THE CREDENTIALS ARE NOT IN THE FIRMWARE. This is the whole point of doing
    it this way: the image carries no SSID and no passphrase, so a leaked .bin
    leaks nothing, and an OTA update cannot carry a credential into a place it
    should not be. They live in the `kvs` NVS partition, which sits outside both
    image slots and therefore survives every firmware update.

    app/app_secrets.h is gitignored. The WiFi credentials in it are read only
    by THIS script and pushed to NVS - but the file IS a build input, so do
    not treat an edit here as provisioning-only: app/src/auth.c pulls it in
    through __has_include("../app_secrets.h") and compiles SECRET_API_TOKEN
    into the image. Changing the token therefore needs a rebuild and a flash,
    and nothing rotates it per version. app_secrets.h.example is the committed
    template, made by blanking every value rather than by copying this file.

    Run once per board. After that the board rejoins by itself on every boot
    (app/src/net.c), which is what makes OTA repeatable rather than one-shot.

.PARAMETER Port
    Serial port of the Zephyr shell. Auto-detected if omitted.

.PARAMETER Show
    Print the passphrase. Off by default, and deliberately awkward to turn on.

.EXAMPLE
    pwsh tools/wifi_provision.ps1
#>
[CmdletBinding()]
param(
    [string]$Port,
    [switch]$Show
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$secrets = Join-Path $repo 'app\app_secrets.h'

if (-not (Test-Path $secrets)) {
    throw @"
$secrets not found.

Copy app/app_secrets.h.example to app/app_secrets.h and fill in the two
values. The example carries placeholders only; it is never made by copying a
working file, because that is how a real credential reaches a repository.
"@
}

$text = Get-Content $secrets -Raw
function Get-Secret([string]$name) {
    if ($text -match ('#define\s+' + $name + '\s+"([^"]*)"')) { return $Matches[1] }
    throw "$name not found in app/app_secrets.h"
}
$ssid = Get-Secret 'SECRET_SSID'
$pass = Get-Secret 'SECRET_PASS'

if ([string]::IsNullOrWhiteSpace($ssid) -or [string]::IsNullOrWhiteSpace($pass)) {
    throw "app/app_secrets.h still has placeholder values - fill it in first."
}

# The board is found below, after Invoke-ZShell is defined.

# REDACTION. The Zephyr shell ECHOES what it is sent, so the board's reply to
# the add command contains the passphrase verbatim. Every line printed from
# here on is passed through this first. The SSID is left alone - it is
# broadcast over the air by definition and is not a secret - but the
# passphrase is replaced wherever it appears, including inside the echo.
function Hide-Secret([string]$s) {
    if ($Show) { return $s }
    if ([string]::IsNullOrEmpty($s)) { return $s }
    return $s.Replace($pass, '<passphrase redacted>')
}

function Invoke-ZShell {
    param([string]$Cmd, [int]$QuietMs = 2500)
    $sp = New-Object System.IO.Ports.SerialPort $Port, 115200, 'None', 8, 'One'
    $sp.ReadTimeout = 500
    try {
        $sp.Open(); $sp.DtrEnable = $true; Start-Sleep -Milliseconds 450
        $sp.DiscardInBuffer()
        $sp.Write("$Cmd`r`n")
        $sb = New-Object System.Text.StringBuilder
        $last = [Diagnostics.Stopwatch]::StartNew()
        while ($last.ElapsedMilliseconds -lt $QuietMs) {
            try { $c = $sp.ReadExisting(); if ($c) { [void]$sb.Append($c); $last.Restart() } } catch {}
            Start-Sleep -Milliseconds 60
        }
        return $sb.ToString()
    } finally { if ($sp.IsOpen) { $sp.Close() } }
}


# --- FIND THE RIGHT BOARD, BY ASKING IT --------------------------------------
#
# Not by port number, and not by VID:PID. More than one Zephyr board lives on
# this bench, they expose identical CDC-ACM shells, and at least one enumerates
# with the same VID:PID as this one in some states. Picking by position has
# already sent a command to the wrong board in this project.
#
# `mdnsname` answers with the hostname compiled into the image, so a board that
# replies "portentaexplorer" is this project's firmware and nothing else is.
# A board running something that does not implement the command simply does not
# answer, which is the correct outcome.
function Find-PmcPort {
    param([string]$Preferred)

    $candidates = if ($Preferred) { @($Preferred) } else {
        [System.IO.Ports.SerialPort]::GetPortNames()
    }

    foreach ($c in $candidates) {
        try {
            $r = Invoke-ZShell -Port $c -Cmd 'mdnsname' -QuietMs 1800
            if ($r -match 'portentaexplorer') { return $c }
        } catch { continue }
    }
    return $null
}

$Port = Find-PmcPort -Preferred $Port
if (-not $Port) {
    throw @"
No port answered as this project's firmware.

Every serial port was asked `mdnsname` and none replied "portentaexplorer".
Either the board is not running this firmware, or it is in the bootloader.
Deliberately NOT falling back to "whichever port looks likely": there is more
than one Zephyr board on this bench and sending credentials to the wrong one
is not a recoverable mistake.
"@
}

Write-Host ""
Write-Host "port         $Port"
Write-Host "ssid         $ssid"
Write-Host ("passphrase   {0}" -f $(if ($Show) { $pass } else { "$($pass.Length) characters, not shown" }))
Write-Host ""

# -k 1 is WPA2-PSK. The CYW4343W is 802.11 b/g/n, so a 5 GHz-only SSID will
# never be found no matter how correct the passphrase is - worth remembering
# before debugging the credential.
Write-Host "==> storing in NVS" -ForegroundColor Cyan
$r = Invoke-ZShell -Cmd ('wifi cred add -s "{0}" -k 1 -p "{1}"' -f $ssid, $pass)
Hide-Secret $r | Write-Host

Write-Host "==> stored networks" -ForegroundColor Cyan
Hide-Secret (Invoke-ZShell -Cmd 'wifi cred list') | Write-Host

Write-Host "==> connecting" -ForegroundColor Cyan
Hide-Secret (Invoke-ZShell -Cmd 'wifi cred auto_connect' -QuietMs 15000) | Write-Host

Start-Sleep -Seconds 5
Write-Host "==> interface" -ForegroundColor Cyan
Hide-Secret (Invoke-ZShell -Cmd 'net iface' -QuietMs 3000) | Write-Host

Write-Host ""
Write-Host "If an IPv4 address appeared above, the board is now updatable over the" -ForegroundColor DarkGray
Write-Host "air and this is the last time it needs a cable: tools/ota.ps1 from here." -ForegroundColor DarkGray
