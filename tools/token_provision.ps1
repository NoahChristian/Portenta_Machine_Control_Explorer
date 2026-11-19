<#
.SYNOPSIS
    Push the write token into the board's NVS, once per board.

.DESCRIPTION
    THE TOKEN IS NOT IN THE FIRMWARE. Until 2026-11-19 it was: app/src/auth.c
    pulled SECRET_API_TOKEN out of app_secrets.h and compared it as a string
    literal, so it sat in zephyr.signed.bin as plain ASCII and `strings` was
    the whole attack. That was tolerable while the image never left the bench
    and stopped being so the moment a public release could carry it.

    It now lives in the `kvs` NVS partition, which sits outside both image
    slots and therefore survives every firmware update - the same arrangement
    the WiFi credentials have had all along (tools/wifi_provision.ps1). Three
    things follow, and the third is the one people miss:

      - a published .bin carries no secret at all
      - two boards running the identical image hold different tokens
      - rotating the token needs no rebuild and no flash, only this script

    RUN THIS AFTER THE FIRST FLASH OF 0.8.0 OR LATER. A board updated from an
    older image has nothing in NVS, serves the panel read-only and refuses
    every write - including the one tools/otahttp.ps1 needs. That is the
    correct failure and it is recoverable over this cable, but it does mean
    the order is: flash, then provision, then OTA works again.

.PARAMETER Port
    Serial port of the Zephyr shell. Auto-detected if omitted.

.PARAMETER Token
    The token to store. Read from app/app_secrets.h when omitted.

.PARAMETER Generate
    Generate a fresh 32-character token, store it, and write it back into
    app/app_secrets.h so the flashing tools can still find it. Use this to
    rotate, or on a board that has never had one.

.PARAMETER Show
    Print the token. Off by default, and deliberately awkward to turn on.

.EXAMPLE
    pwsh tools/token_provision.ps1
    pwsh tools/token_provision.ps1 -Generate
#>
[CmdletBinding()]
param(
    [string]$Port,
    [string]$Token,
    [switch]$Generate,
    [switch]$Show
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'pmcshell.ps1')

$repo = Split-Path -Parent $PSScriptRoot
$secrets = Join-Path $repo 'app\app_secrets.h'

# Mirrors PMC_AUTH_TOKEN_MIN / _MAX in app/src/auth.h. Checked here as well as
# on the board so a too-short token is refused before it is echoed over a
# serial line, rather than after.
$MIN = 16
$MAX = 64

if ($Generate) {
    if ($Token) { throw "-Generate and -Token are alternatives, not a pair" }
    # RNGCryptoServiceProvider is obsolete in .NET 6+; this is the supported
    # call and it is the one that must be used for a credential. Base64 of 24
    # bytes is 32 characters, and the URL-unsafe pair is replaced because the
    # token travels in a form field.
    $bytes = [byte[]]::new(24)
    [System.Security.Cryptography.RandomNumberGenerator]::Fill($bytes)
    $Token = [Convert]::ToBase64String($bytes).Replace('+', '-').Replace('/', '_').TrimEnd('=')
}

if (-not $Token) {
    $Token = Get-SecretFromHeader -Path $secrets -Name 'SECRET_API_TOKEN'
}

if ([string]::IsNullOrWhiteSpace($Token)) {
    throw "no token: pass -Token, use -Generate, or fill SECRET_API_TOKEN in app/app_secrets.h"
}
if ($Token.Length -lt $MIN -or $Token.Length -gt $MAX) {
    throw "token is $($Token.Length) characters; the board accepts $MIN..$MAX (app/src/auth.h)"
}

# REDACTION. The Zephyr shell ECHOES what it is sent, so the board's reply to
# `token set` contains the token verbatim. Every line printed from here on is
# passed through this first.
function Hide-Token([string]$s) {
    if ($Show) { return $s }
    if ([string]::IsNullOrEmpty($s)) { return $s }
    return $s.Replace($Token, '<token redacted>')
}

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
Write-Host ("token        {0}" -f $(if ($Show) { $Token } else { "$($Token.Length) characters, not shown" }))
Write-Host ""

Write-Host "==> storing in NVS" -ForegroundColor Cyan
Hide-Token (Invoke-ZShell -Port $Port -Cmd ('token set "{0}"' -f $Token)) | Write-Host

Write-Host "==> state" -ForegroundColor Cyan
$status = Invoke-ZShell -Port $Port -Cmd 'token status'
Hide-Token $status | Write-Host

# THE SCRIPT ASSERTS ITS OWN RESULT. `token status` reports the LENGTH and
# never the value, which is exactly enough to check without printing anything:
# a board that stored nothing says "no token", and one that stored a truncated
# line says a different number. Without this the script would report success
# on any reply at all, which is the failure mode this repo keeps finding.
if ($status -notmatch ('provisioned, {0} characters' -f $Token.Length)) {
    throw @"
The board did not confirm a $($Token.Length)-character token.

It replied:
$(Hide-Token $status)
"@
}

if ($Generate) {
    # Write it back so otahttp.ps1 and the panel still have somewhere to read
    # it from. app_secrets.h is gitignored and is no longer a build input -
    # nothing here is compiled into the image any more.
    $text = Get-Content $secrets -Raw
    $new = [regex]::Replace($text, '(#define\s+SECRET_API_TOKEN\s+")([^"]*)(")',
                            { param($m) $m.Groups[1].Value + $Token + $m.Groups[3].Value })
    if ($new -eq $text) {
        throw "SECRET_API_TOKEN not found in $secrets - the new token is on the board but is written down nowhere"
    }
    Set-Content -Path $secrets -Value $new -NoNewline
    Write-Host ""
    Write-Host "app/app_secrets.h updated with the new token." -ForegroundColor DarkGray
}

Write-Host ""
Write-Host "Stored in the kvs partition, outside both image slots, so it" -ForegroundColor DarkGray
Write-Host "survives every OTA. The image itself now carries no secret." -ForegroundColor DarkGray
