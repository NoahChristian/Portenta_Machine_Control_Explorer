<#
.SYNOPSIS
    Stage a signed image in slot1 over HTTP, then optionally arm it.

.DESCRIPTION
    THE POINT OF THIS SCRIPT IS TCP. tools/ota.ps1 uses smpmgr over UDP, and
    SMP over UDP is lockstep with no window: one lost datagram ends the
    transfer, and the protocol has nothing to resend it with. Measured on this
    bench, loss is about 0.06% per datagram - invisible to a ping test, and
    fatal to a 1,100-datagram upload roughly half the time. See
    docs/MEASURED.md, "The OTA stall, caught on the console".

    This posts the same signed image to POST /ota, which writes it into the
    secondary slot through the same flash_img API that img_mgmt uses. MCUboot's
    slot logic and signature check are untouched; only the transport changes.

    ARMING IS SEPARATE, and deliberately so. Staging writes slot1; arming
    changes what the board boots. Pass -Arm to do both, or run the script
    twice.

.PARAMETER Ip
    The board's address or mDNS name.

.PARAMETER Token
    The API token. Defaults to $env:PMC_API_TOKEN so it stays out of shell
    history and out of this file.

.PARAMETER Arm
    After a successful upload, POST do=otatest to mark slot1 pending. The image
    still has to pass MCUboot's signature check, and then our own health gate
    before it confirms itself - so an image that does not work reverts on the
    reset after next.

.PARAMETER Reset
    With -Arm, also reboot so the swap happens now.

.EXAMPLE
    pwsh tools/otahttp.ps1 -Ip portentaexplorer.local
    pwsh tools/otahttp.ps1 -Ip portentaexplorer.local -Arm -Reset
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Ip,
    [string]$Token = $env:PMC_API_TOKEN,
    [switch]$Arm,
    [switch]$Reset,
    [int]$TimeoutSec = 300
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$img = Join-Path $repo 'build\app\zephyr\zephyr.signed.bin'

if (-not (Test-Path $img)) { throw "not built: $img" }
if (-not $Token) {
    throw 'no token: pass -Token or set $env:PMC_API_TOKEN'
}

# THE SIGNED IMAGE, NOT THE PLAIN ONE. zephyr.bin has its vector table at
# offset 0 and MCUboot expects it after a 1 KB header, so a plain image is
# accepted by every length check here and then refused at boot, which looks
# like a transport failure. The magic is checked rather than the filename.
$bytes = [System.IO.File]::ReadAllBytes($img)
$magic = ($bytes[0..3] | ForEach-Object { $_.ToString('x2') }) -join ''
if ($magic -ne '3db8f396') {
    throw "$img is not a signed MCUboot image (magic 0x$magic)"
}

Write-Host "==> staging $($bytes.Length) bytes over HTTP" -ForegroundColor Cyan

$sw = [System.Diagnostics.Stopwatch]::StartNew()
try {
    $r = Invoke-WebRequest -Uri "http://$Ip/ota" -Method Post `
        -Headers @{ 'X-PMC-Token' = $Token } `
        -ContentType 'application/octet-stream' `
        -Body $bytes -TimeoutSec $TimeoutSec -UseBasicParsing
} catch {
    throw "upload failed after $([int]$sw.Elapsed.TotalSeconds) s: $($_.Exception.Message)"
}
$sw.Stop()

$say = ($r.Content | Out-String).Trim()
Write-Host "    $say" -ForegroundColor Green
Write-Host ("    {0:N0} s, {1:N0} kB/s" -f $sw.Elapsed.TotalSeconds,
    ($bytes.Length / 1024 / [math]::Max($sw.Elapsed.TotalSeconds, 1)))

# THE BOARD'S OWN COUNT IS THE CHECK, not the HTTP status. A 200 means the
# request completed; it does not mean every byte reached flash.
if ($say -notmatch "staged\s+$($bytes.Length)\s+bytes") {
    throw "the board staged a different length than was sent - do not arm this"
}

if (-not $Arm) {
    Write-Host ""
    Write-Host "not armed. To arm:  pwsh tools/otahttp.ps1 -Ip $Ip -Arm" -ForegroundColor Yellow
    exit 0
}

Write-Host "==> arming slot1" -ForegroundColor Cyan
$body = "token=$([uri]::EscapeDataString($Token))&do=otatest"
$a = Invoke-WebRequest -Uri "http://$Ip/io" -Method Post `
    -ContentType 'application/x-www-form-urlencoded' `
    -Body $body -TimeoutSec 30 -UseBasicParsing
$asay = ($a.Content | Out-String).Trim()
Write-Host "    $asay" -ForegroundColor Green
if ($asay -match '^refused') { throw 'arming refused' }

if (-not $Reset) {
    Write-Host ""
    Write-Host 'armed but not reset. The swap happens on the next boot.' -ForegroundColor Yellow
    exit 0
}

Write-Host "==> resetting" -ForegroundColor Cyan
try {
    Invoke-WebRequest -Uri "http://$Ip/reset" -Method Post `
        -ContentType 'application/x-www-form-urlencoded' `
        -Body "token=$([uri]::EscapeDataString($Token))" `
        -TimeoutSec 10 -UseBasicParsing | Out-Null
} catch {
    # The board reboots while answering, so a dropped connection here is the
    # expected outcome rather than a failure.
    Write-Host '    (connection dropped, which is what a reboot looks like)' -ForegroundColor DarkGray
}
Write-Host ''
Write-Host 'Reset sent. It swaps, then confirms itself only when every health' -ForegroundColor Yellow
Write-Host 'check passes - otherwise it reverts on the reset after next.' -ForegroundColor Yellow
