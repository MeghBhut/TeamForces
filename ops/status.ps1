#requires -Version 5.1
<#
    Shows whether the background services are up, and on which addresses.
    Safe to run any time; changes nothing.
#>
[CmdletBinding()]
param()

$Services = @(
    @{ Name = 'TeamForces';  Port = 8090 },
    @{ Name = 'LabelStudio'; Port = 8080 }
)

$tsIp = (Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
         Where-Object { $_.IPAddress -like '100.*' } |
         Select-Object -First 1).IPAddress

foreach ($svc in $Services) {
    $conn = Get-NetTCPConnection -State Listen -LocalPort $svc.Port -ErrorAction SilentlyContinue |
            Select-Object -First 1

    if (-not $conn) {
        Write-Host ("{0,-13} DOWN" -f $svc.Name)
        continue
    }

    $proc = Get-Process -Id $conn.OwningProcess -ErrorAction SilentlyContinue
    $pname = if ($proc) { $proc.ProcessName } else { '?' }

    Write-Host ("{0,-13} UP   port {1}  (PID {2}, {3})" -f $svc.Name, $svc.Port, $conn.OwningProcess, $pname)
    Write-Host ("              http://localhost:{0}" -f $svc.Port)
    if ($tsIp) {
        Write-Host ("              http://{0}:{1}   (tailnet)" -f $tsIp, $svc.Port)
    }
}
