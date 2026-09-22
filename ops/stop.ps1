#requires -Version 5.1
<#
    Stops the background services started by launch.ps1.

    Safety: a PID is only killed if the running process's command line still
    matches the service it is supposed to be. PIDs get recycled by Windows, and
    "kill every python.exe" would take out unrelated work.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$OpsDir = $PSScriptRoot
$RunDir = Join-Path $OpsDir 'run'

$Services = @(
    @{ Name = 'TeamForces';  Port = 8090; CmdLineTag = 'teamforces.exe' },
    @{ Name = 'LabelStudio'; Port = 8080; CmdLineTag = 'label-studio'   }
)

function Get-CmdLine {
    param([int]$ProcId)
    $p = Get-CimInstance Win32_Process -Filter "ProcessId=$ProcId" -ErrorAction SilentlyContinue
    if ($p) { return $p.CommandLine }
    return $null
}

foreach ($svc in $Services) {
    $name    = $svc.Name
    $pidFile = Join-Path $RunDir "$name.pid"
    $target  = $null

    # Prefer the PID we recorded; fall back to whoever owns the port, so services
    # started by hand (start.bat, a terminal) can still be stopped.
    if (Test-Path $pidFile) {
        $recorded = (Get-Content $pidFile -Raw).Trim()
        if ($recorded -match '^\d+$') { $target = [int]$recorded }
    }

    if (-not $target) {
        $conn = Get-NetTCPConnection -State Listen -LocalPort $svc.Port -ErrorAction SilentlyContinue |
                Select-Object -First 1
        if ($conn) { $target = [int]$conn.OwningProcess }
    }

    if (-not $target) {
        Write-Host "$name : not running."
        continue
    }

    $cmdline = Get-CmdLine -ProcId $target
    if (-not $cmdline) {
        Write-Host "$name : PID $target is gone already."
        if (Test-Path $pidFile) { Remove-Item $pidFile -Force }
        continue
    }

    if ($cmdline -notlike "*$($svc.CmdLineTag)*") {
        Write-Warning "$name : PID $target does not look like $name (command line: $cmdline). Skipping to avoid killing the wrong process."
        continue
    }

    try {
        Stop-Process -Id $target -Force -ErrorAction Stop
        Write-Host "$name : stopped (PID $target)."
    } catch {
        Write-Warning "$name : could not stop PID $target -- $($_.Exception.Message)"
        continue
    }

    if (Test-Path $pidFile) { Remove-Item $pidFile -Force }
}
