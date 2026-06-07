$ErrorActionPreference = "Stop"
. "$PSScriptRoot\common.ps1"

Write-Step "Checking LiquidOS tools"

$missing = $false

try {
    $nasm = Find-Nasm
    Write-Host "NASM: $nasm"
    & $nasm -v
}
catch {
    Write-Host "NASM: missing" -ForegroundColor Red
    Write-Host $_.Exception.Message
    $missing = $true
}

try {
    $clang = Find-Clang
    Write-Host "LLVM clang: $clang"
    & $clang --version | Select-Object -First 1
}
catch {
    Write-Host "LLVM clang: missing" -ForegroundColor Red
    Write-Host $_.Exception.Message
    $missing = $true
}

try {
    $lld = Find-Lld
    Write-Host "LLVM lld: $lld"
    & $lld --version
}
catch {
    Write-Host "LLVM lld: missing" -ForegroundColor Red
    Write-Host $_.Exception.Message
    $missing = $true
}

try {
    $objcopy = Find-Objcopy
    Write-Host "LLVM objcopy: $objcopy"
    & $objcopy --version | Select-Object -First 1
}
catch {
    Write-Host "LLVM objcopy: missing" -ForegroundColor Red
    Write-Host $_.Exception.Message
    $missing = $true
}

try {
    $vbox = Find-VBoxManage
    Write-Host "VirtualBox VBoxManage: $vbox"
    & $vbox --version
}
catch {
    Write-Host "VirtualBox: missing" -ForegroundColor Red
    Write-Host $_.Exception.Message
    $missing = $true
}

if ($missing) {
    Write-Host ""
    Write-Host "Install the missing tools, close this PowerShell window, open a new one, and run this script again." -ForegroundColor Yellow
    exit 1
}

Write-Host ""
Write-Host "Success: all LiquidOS tools are available." -ForegroundColor Green
