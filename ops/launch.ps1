#requires -Version 5.1
<#
    Starts TeamForces + Label Studio silently in the background, waits until both
    are actually serving, then locks the workstation.

    Locking does NOT stop the processes -- Windows keeps background processes
    running across a lock. You stay reachable over Tailscale while the machine
    sits locked in an empty room.

    Usage:
        Start Services.vbs        <- normal use, zero visible windows
        powershell -File launch.ps1 -NoLock     <- start but leave screen unlocked
#>
[CmdletBinding()]
param(
    # Start the services but skip the screen lock (useful when testing).
    [switch]$NoLock
)

$ErrorActionPreference = 'Stop'

$OpsDir = $PSScriptRoot
$Root   = Split-Path -Parent $OpsDir     # E:\Teamforces
$LogDir = Join-Path $OpsDir 'logs'
$RunDir = Join-Path $OpsDir 'run'

foreach ($d in @($LogDir, $RunDir)) {
    if (-not (Test-Path $d)) { New-Item -ItemType Directory -Path $d | Out-Null }
}

# Label Studio lives in its own virtualenv on D:. The globally installed
# label-studio.exe is broken (setuptools 82 removed pkg_resources), so we
# deliberately point at the venv copy and never touch the global one.
$LabelStudioExe = 'D:\ls_env\Scripts\label-studio.exe'

$Services = @(
    @{
        Name        = 'TeamForces'
        Exe         = Join-Path $Root 'teamforces.exe'
        Args        = @('--port', '8090')
        WorkDir     = $Root
        Port        = 8090
        TimeoutSec  = 20
        # Substring that must appear in a process's command line before we are
        # willing to kill it, so stop.ps1 can never take out an unrelated process.
        CmdLineTag  = 'teamforces.exe'
    },
    @{
        Name        = 'LabelStudio'
        Exe         = $LabelStudioExe
        Args        = @('start', '--no-browser', '-p', '8080')
        WorkDir     = $env:USERPROFILE
        Port        = 8080
        TimeoutSec  = 120   # Django boot is slow on a cold start
        CmdLineTag  = 'label-studio'
    }
)

function Test-PortListening {
    param([int]$Port)
    $conn = Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue
    return [bool]$conn
}

function Show-Popup {
    param([string]$Message, [string]$Title = 'TeamForces launcher')
    # The launcher runs with no console, so failures need a real dialog or they
    # would vanish silently.
    (New-Object -ComObject WScript.Shell).Popup($Message, 0, $Title, 0x30) | Out-Null
}

$problems = @()

foreach ($svc in $Services) {
    $name    = $svc.Name
    $pidFile = Join-Path $RunDir "$name.pid"

    if (Test-PortListening -Port $svc.Port) {
        # Already up (either from a previous launch or started by hand). Leave it
        # alone -- starting a second copy would just fail to bind the port.
        continue
    }

    if (-not (Test-Path $svc.Exe)) {
        $problems += "$name`: executable not found at $($svc.Exe)"
        continue
    }

    $outLog = Join-Path $LogDir "$name.out.log"
    $errLog = Join-Path $LogDir "$name.err.log"

    try {
        $proc = Start-Process -FilePath $svc.Exe `
                              -ArgumentList $svc.Args `
                              -WorkingDirectory $svc.WorkDir `
                              -WindowStyle Hidden `
                              -RedirectStandardOutput $outLog `
                              -RedirectStandardError $errLog `
                              -PassThru
    } catch {
        $problems += "$name`: failed to start -- $($_.Exception.Message)"
        continue
    }

    Set-Content -Path $pidFile -Value $proc.Id -Encoding ascii

    # Wait for the port to actually accept connections. A process that exists but
    # never binds (bad args, port stolen, crash on boot) must not be reported as up.
    $deadline = (Get-Date).AddSeconds($svc.TimeoutSec)
    $ready    = $false
    while ((Get-Date) -lt $deadline) {
        if ($proc.HasExited) { break }
        if (Test-PortListening -Port $svc.Port) { $ready = $true; break }
        Start-Sleep -Milliseconds 500
    }

    if (-not $ready) {
        if ($proc.HasExited) {
            $problems += "$name`: process exited immediately (exit code $($proc.ExitCode)). See $errLog"
        } else {
            $problems += "$name`: still not listening on port $($svc.Port) after $($svc.TimeoutSec)s. See $errLog"
        }
    }
}

if ($problems.Count -gt 0) {
    # Deliberately do NOT lock the screen -- locking now would hide a broken
    # service behind a lock screen with no way to notice.
    Show-Popup -Message ("Not everything started, so the screen was left unlocked:`n`n" +
                         ($problems -join "`n`n"))
    exit 1
}

if (-not $NoLock) {
    # Flush the log writes before the session locks.
    Start-Sleep -Milliseconds 300
    rundll32.exe user32.dll,LockWorkStation
}

exit 0
