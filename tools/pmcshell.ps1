<#
.SYNOPSIS
    Shared serial-shell plumbing for the provisioning scripts. Dot-source it;
    it defines functions and does nothing on its own.

.DESCRIPTION
    ONE COPY, BECAUSE TWO WOULD DRIFT. wifi_provision.ps1 and
    token_provision.ps1 both have to open the right serial port, send a line
    to the Zephyr shell and read the reply until it goes quiet. That is forty
    lines of fiddly plumbing with a hard-won rule inside it - see
    Find-PmcPort - and a second copy of a hard-won rule is a copy that stops
    being hard-won the first time only one of them is fixed.

    Dot-source from a script in this directory:

        . (Join-Path $PSScriptRoot 'pmcshell.ps1')
#>

function Invoke-ZShell {
    <#
    .SYNOPSIS
        Send one line to the board's shell and return everything it says back.

    .DESCRIPTION
        READS UNTIL QUIET, not until a prompt. The Zephyr shell's prompt is
        configurable and its banner is not guaranteed, so waiting for a
        specific string is a bet on a build; waiting for the port to stop
        producing bytes is a property of the transport. QuietMs is how long
        silence has to last before the reply is considered finished, and a
        command that thinks before answering - `wifi cred auto_connect` - just
        needs a bigger one.

        The port is opened and closed per call deliberately. Holding it across
        a reset leaves a handle on a device that enumerated again underneath
        it, and the board resets more than once in a provisioning session.

        MaxMs IS NOT OPTIONAL POLISH, it is what makes this terminate. "Read
        until quiet" with no ceiling is a live-lock against a board that logs:
        anything printing more often than QuietMs restarts the quiet timer
        forever, and the call never returns. The quiet timer decides when the
        REPLY is finished; this decides when to stop waiting for one.

        THAT IS NOT HYPOTHETICAL AND IT COST A SESSION. With an RTD front end
        unattached, this board logs

            <err> MAX31865: Fault Register: 0x04, Over/under voltage fault

        continuously. Measured on the bench with DTR asserted: 3129 bytes in
        6 s, LONGEST SILENCE 599 ms. Every QuietMs these tools pass - 1800,
        2500, 3000 - is longer than that, so the quiet timer restarted before
        its window could ever close and the loop never exited. ota.ps1 sat for
        over ten minutes having printed nothing, on a board that was answering
        its console and serving its web panel the whole time.

        A faulting sensor is an ORDINARY state for a bench board. A routine
        that cannot survive one is a routine that works until the day it is
        needed.

        BEWARE MEASURING THIS WITHOUT DTR. Zephyr's CDC-ACM sends nothing
        until DTR is asserted, so a probe that omits it reports the console
        silent and appears to exonerate exactly this cause. That misreading is
        why the fix took two attempts.

        A capped read still works, because callers match on CONTENT - the mDNS
        name, a dotted quad - and a reply interleaved with log lines still
        contains it. What a cap cannot do is turn silence into an answer,
        which is why it says when it fired rather than returning a short read
        that looks like a complete one.

        THE CEILING IS DERIVED FROM THE QUIET WINDOW, not a flat number. Four
        quiet windows with an 8 s floor: long enough that a merely slow reply
        still finishes normally, short enough that a stuck port is a pause
        rather than a hang - and it scales, so a caller that raises QuietMs for
        a command that thinks before answering does not also have to remember
        to raise the ceiling. Pass MaxMs to override it outright.

        AND IT SAYS SO WHEN IT FIRES. A cap that truncates silently is the same
        class of problem as the hang it replaced: the caller cannot tell a
        complete reply from a cut-off one. Expect to see it on this bench
        routinely - the board logs an open-RTD fault about ten times a second
        with no probe connected, which is faster than any quiet window, so the
        ceiling rather than the silence is what ends most reads here.

        THIS FUNCTION HAD TWO COPIES AND THEY DRIFTED. ota.ps1 carried its own
        with the derived ceiling and this warning; this one had a flat
        MaxMs = 20000 and neither. Both terminated, so the live-lock lesson had
        reached both - but the better half of it lived only in the copy nobody
        was reading. Unified here on 2026-11-20, taking the better half of each.
    #>
    param(
        [Parameter(Mandatory = $true)][string]$Port,
        [Parameter(Mandatory = $true)][string]$Cmd,
        [int]$QuietMs = 2500,
        # 0 means derive it from QuietMs; see above.
        [int]$MaxMs = 0
    )
    if ($MaxMs -le 0) { $MaxMs = [Math]::Max(8000, $QuietMs * 4) }

    $sp = New-Object System.IO.Ports.SerialPort $Port, 115200, 'None', 8, 'One'
    $sp.ReadTimeout = 500
    try {
        $sp.Open(); $sp.DtrEnable = $true; Start-Sleep -Milliseconds 450
        $sp.DiscardInBuffer()
        $sp.Write("$Cmd`r`n")
        $sb = New-Object System.Text.StringBuilder
        $last = [Diagnostics.Stopwatch]::StartNew()
        $total = [Diagnostics.Stopwatch]::StartNew()
        while ($last.ElapsedMilliseconds -lt $QuietMs -and
               $total.ElapsedMilliseconds -lt $MaxMs) {
            try { $c = $sp.ReadExisting(); if ($c) { [void]$sb.Append($c); $last.Restart() } } catch {}
            Start-Sleep -Milliseconds 60
        }
        if ($total.ElapsedMilliseconds -ge $MaxMs) {
            Write-Host ("  {0} kept talking for {1} ms; reading what arrived" `
                        -f $Port, $MaxMs) -ForegroundColor DarkYellow
        }
        return $sb.ToString()
    } finally { if ($sp.IsOpen) { $sp.Close() } }
}

function Find-PmcPort {
    <#
    .SYNOPSIS
        Find this project's board by ASKING each serial port what it is.

    .DESCRIPTION
        Not by port number, and not by VID:PID. More than one Zephyr board
        lives on this bench, they expose identical CDC-ACM shells, and at
        least one enumerates with the same VID:PID as this one in some states.
        Picking by position has already sent a command to the wrong board in
        this project.

        `mdnsname` answers with the hostname compiled into the image, so a
        board that replies "portentaexplorer" is this project's firmware and
        nothing else is. A board running something that does not implement the
        command simply does not answer, which is the correct outcome.

        Returns $null when nothing answers. Deliberately NOT falling back to
        "whichever port looks likely": sending a credential to the wrong board
        is not a recoverable mistake.
    #>
    param([string]$Preferred)

    $candidates = if ($Preferred) { @($Preferred) } else {
        [System.IO.Ports.SerialPort]::GetPortNames()
    }

    foreach ($c in $candidates) {
        try {
            # A tight ceiling here on purpose: this runs once per port, and a
            # port that is not the board must cost a couple of seconds, not a
            # couple of minutes. Log noise from the real board still matches,
            # because the reply is searched rather than parsed.
            $r = Invoke-ZShell -Port $c -Cmd 'mdnsname' -QuietMs 1800 -MaxMs 4000
            if ($r -match 'portentaexplorer') { return $c }
        } catch { continue }
    }
    return $null
}

function Get-PmcHost {
    <#
    .SYNOPSIS
        The board's mDNS name, read from the firmware that answers to it.

    .DESCRIPTION
        ADDRESS THE BOARD BY NAME. A DHCP lease is not a property of the unit,
        and this one has already moved to a different SUBNET - not just a
        different host - the moment Ethernet came up and the radio went down.
        That is the designed behavior: Ethernet preferred, radio as fallback,
        re-decided on every link poll. It silently invalidates any address
        written down earlier, and the name follows the board across the switch.

        IT IS READ FROM app/prj.conf RATHER THAN WRITTEN HERE. CONFIG_NET_HOSTNAME
        is what the firmware actually registers with the mDNS responder, so a
        copy of the name in each tool would be four places to change and three
        of them would be missed. Throws rather than guessing a default: a tool
        that falls back to a hard-coded name after failing to read the real one
        is a tool that talks to the wrong board on the day they differ.

        A NOTE ON RESOLUTION, because the obvious check lies. `Resolve-DnsName
        portentaexplorer.local` FAILS on this PC - it queries unicast DNS - while
        Invoke-WebRequest to the same name succeeds, because the Windows socket
        resolver handles .local through mDNS. Anything resolving through the
        normal socket path works: PowerShell's web cmdlets, .NET, Python, and
        therefore smpmgr.
    #>
    param([string]$Repo)

    if (-not $Repo) { $Repo = Split-Path -Parent $PSScriptRoot }
    $conf = Join-Path $Repo 'app\prj.conf'
    if (-not (Test-Path $conf)) {
        throw "$conf not found - cannot read the board's mDNS name"
    }
    $text = Get-Content $conf -Raw
    if ($text -match '(?m)^\s*CONFIG_NET_HOSTNAME\s*=\s*"([^"]+)"') {
        return "$($Matches[1]).local"
    }
    throw @"
CONFIG_NET_HOSTNAME not found in $conf.

That symbol is what the firmware registers with the mDNS responder, so without
it there is no name to address the board by. Pass -Target <address> to aim at
one directly.
"@
}

function Get-SecretFromHeader {
    <#
    .SYNOPSIS
        Read one #define out of app/app_secrets.h.

    .DESCRIPTION
        That file is gitignored and is NOT a build input any more - since
        2026-11-19 nothing in it is compiled into the image, and it exists
        only so the provisioning scripts have somewhere to read from. Throws
        when the define is absent, so a renamed value fails loudly instead of
        provisioning an empty string.
    #>
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Name
    )
    if (-not (Test-Path $Path)) {
        throw @"
$Path not found.

Copy app/app_secrets.h.example to app/app_secrets.h and fill it in. The
example carries placeholders only; it is never made by copying a working
file, because that is how a real credential reaches a repository.
"@
    }
    $text = Get-Content $Path -Raw
    if ($text -match ('#define\s+' + $Name + '\s+"([^"]*)"')) { return $Matches[1] }
    throw "$Name not found in $Path"
}
