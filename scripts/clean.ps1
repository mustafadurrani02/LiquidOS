$ErrorActionPreference = "Stop"
. "$PSScriptRoot\common.ps1"

$buildDir = Get-BuildDir
Write-Step "Cleaning build outputs"

Get-ChildItem -Path $buildDir -File -Include *.bin, *.img, *.iso, *.elf, *.o, *.inc -Recurse | Remove-Item -Force

Write-Host "Build outputs removed from $buildDir" -ForegroundColor Green
