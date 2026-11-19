<#
.SYNOPSIS
    Push WiFi credentials from app/app_secrets.h into the board's NVS, once.

.DESCRIPTION
    THE CREDENTIALS ARE NOT IN THE FIRMWARE. This is the whole point of doing
    it this way: the image carries no SSID and no passphrase, so a leaked .bin
    leaks nothing, and an OTA update cannot carry a credential into a place it
    should not be. They live in the `kvs` NVS partition, which sits outside both
    image slots and therefore survives every firmware update.

    app/app_secrets.h is gitignored and, since 2026-11-19, is NOT a build
    input at all: nothing in it reaches the image. This paragraph used to warn
    the opposite, because app/src/auth.c pulled the file in through
    __has_include("../app_secrets.h") and compiled SECRET_API_TOKEN into the
    binary - so changing the token meant a rebuild and a flash, and the token
    was recoverable from any copy of the image. The token is provisioned now,
    by tools/token_provision.ps1, exactly as the credentials here are.

    app_secrets.h.example is the committed template, made by blanking every
    value rather than by copying this file.

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

# Invoke-ZShell and Find-PmcPort used to be defined here, and are now in
# tools/pmcshell.ps1 so token_provision.ps1 shares them rather than carrying a
# second copy that stops matching this one. Find-PmcPort in particular holds a
# rule that was paid for - it identifies the board by ASKING it, never by port
# number or VID:PID, because picking by position has already sent a command to
# the wrong board in this project.
. (Join-Path $PSScriptRoot 'pmcshell.ps1')

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
$r = Invoke-ZShell -Port $Port -Cmd ('wifi cred add -s "{0}" -k 1 -p "{1}"' -f $ssid, $pass)
Hide-Secret $r | Write-Host

Write-Host "==> stored networks" -ForegroundColor Cyan
Hide-Secret (Invoke-ZShell -Port $Port -Cmd 'wifi cred list') | Write-Host

Write-Host "==> connecting" -ForegroundColor Cyan
# MaxMs above QuietMs, or the ceiling would cut the wait short: association
# genuinely takes tens of seconds and the quiet timer is what should end it.
Hide-Secret (Invoke-ZShell -Port $Port -Cmd 'wifi cred auto_connect' -QuietMs 15000 -MaxMs 45000) | Write-Host

Start-Sleep -Seconds 5
Write-Host "==> interface" -ForegroundColor Cyan
Hide-Secret (Invoke-ZShell -Port $Port -Cmd 'net iface' -QuietMs 3000) | Write-Host

Write-Host ""
Write-Host "If an IPv4 address appeared above, the board is now updatable over the" -ForegroundColor DarkGray
Write-Host "air and this is the last time it needs a cable: tools/ota.ps1 from here." -ForegroundColor DarkGray
