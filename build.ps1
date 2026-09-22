# build.ps1 — build TeamForces on Windows (MSYS2/MinGW g++).
# Usage:  powershell -ExecutionPolicy Bypass -File build.ps1
#
# Produces a self-contained teamforces.exe (statically linked, so it does not
# need the MSYS2 runtime DLLs on your PATH to run).

$ErrorActionPreference = "Stop"

# Find g++ (prefer the MSYS2 ucrt64 one if it is installed).
$gpp = "g++"
if (Test-Path "C:\msys64\ucrt64\bin\g++.exe") { $gpp = "C:\msys64\ucrt64\bin\g++.exe" }

Write-Host "Compiling with $gpp ..."
& $gpp -std=c++17 -O2 -pthread `
    -static -static-libgcc -static-libstdc++ `
    -I third_party `
    (Get-ChildItem src\*.cpp | ForEach-Object { $_.FullName }) `
    -o teamforces.exe `
    -lws2_32

if ($LASTEXITCODE -ne 0) { Write-Error "build failed"; exit 1 }
Write-Host "OK -> teamforces.exe"
