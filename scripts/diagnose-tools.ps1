$ErrorActionPreference = "Continue"
. "$PSScriptRoot\common.ps1"

function Show-PathCheck {
    param(
        [string]$Label,
        [string]$Path
    )

    if (-not $Path) {
        return
    }

    if (Test-Path $Path) {
        Write-Host "FOUND $Label : $Path" -ForegroundColor Green
    } else {
        Write-Host "missing $Label : $Path" -ForegroundColor DarkGray
    }
}

function Show-CommandCheck {
    param(
        [string]$Label,
        [string]$Command
    )

    $found = Get-Command $Command -ErrorAction SilentlyContinue
    if ($found) {
        Write-Host "FOUND $Label in PATH: $($found.Source)" -ForegroundColor Green
    } else {
        Write-Host "missing $Label in PATH" -ForegroundColor DarkGray
    }
}

Write-Step "Diagnosing tool locations"

Write-Host ""
Write-Host "PowerShell can currently find these through PATH:"
Show-CommandCheck "NASM" "nasm.exe"
Show-CommandCheck "VirtualBox" "VBoxManage.exe"
Show-CommandCheck "LLVM clang" "clang.exe"
Show-CommandCheck "LLVM lld" "ld.lld.exe"
Show-CommandCheck "LLVM objcopy" "llvm-objcopy.exe"

Write-Host ""
Write-Host "Checking common install folders:"
Show-PathCheck "NASM" (Join-Path $env:ProgramFiles "NASM\nasm.exe")
Show-PathCheck "NASM" (Join-Path ${env:ProgramFiles(x86)} "NASM\nasm.exe")
Show-PathCheck "NASM" "C:\NASM\nasm.exe"
Show-PathCheck "VirtualBox" (Join-Path $env:ProgramFiles "Oracle\VirtualBox\VBoxManage.exe")
Show-PathCheck "VirtualBox" (Join-Path ${env:ProgramFiles(x86)} "Oracle\VirtualBox\VBoxManage.exe")
Show-PathCheck "LLVM clang" (Join-Path $env:ProgramFiles "LLVM\bin\clang.exe")
Show-PathCheck "LLVM lld" (Join-Path $env:ProgramFiles "LLVM\bin\ld.lld.exe")
Show-PathCheck "LLVM objcopy" (Join-Path $env:ProgramFiles "LLVM\bin\llvm-objcopy.exe")

Write-Host ""
Write-Host "Checking LiquidOS's resolver:"

try {
    Write-Host "NASM resolves to: $(Find-Nasm)" -ForegroundColor Green
} catch {
    Write-Host "NASM still not resolved: $($_.Exception.Message)" -ForegroundColor Red
}

try {
    Write-Host "VirtualBox resolves to: $(Find-VBoxManage)" -ForegroundColor Green
} catch {
    Write-Host "VirtualBox still not resolved: $($_.Exception.Message)" -ForegroundColor Red
}

try {
    Write-Host "LLVM clang resolves to: $(Find-Clang)" -ForegroundColor Green
    Write-Host "LLVM lld resolves to: $(Find-Lld)" -ForegroundColor Green
    Write-Host "LLVM objcopy resolves to: $(Find-Objcopy)" -ForegroundColor Green
} catch {
    Write-Host "LLVM still not resolved: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""
Write-Host "If a tool is installed but still not resolved, set it manually in this PowerShell window."
Write-Host "Example:"
Write-Host '$env:LIQUIDOS_NASM="C:\Program Files\NASM\nasm.exe"'
Write-Host '$env:LIQUIDOS_VBOXMANAGE="C:\Program Files\Oracle\VirtualBox\VBoxManage.exe"'
Write-Host '$env:LIQUIDOS_LLVM_BIN="C:\Program Files\LLVM\bin"'
