$ErrorActionPreference = "Stop"

$script:ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$script:BuildDir = Join-Path $script:ProjectRoot "build"

function Get-ProjectRoot {
    return $script:ProjectRoot
}

function Get-BuildDir {
    if (-not (Test-Path $script:BuildDir)) {
        New-Item -ItemType Directory -Force -Path $script:BuildDir | Out-Null
    }
    return $script:BuildDir
}

function Write-Step {
    param([string]$Message)
    Write-Host "[LiquidOS] $Message" -ForegroundColor Cyan
}

function Test-ExecutablePath {
    param(
        [string]$Path,
        [string]$ExeName
    )

    if (-not $Path) {
        return $null
    }

    if (Test-Path $Path -PathType Leaf) {
        return (Resolve-Path $Path).Path
    }

    if (Test-Path $Path -PathType Container) {
        $candidate = Join-Path $Path $ExeName
        if (Test-Path $candidate -PathType Leaf) {
            return (Resolve-Path $candidate).Path
        }
    }

    return $null
}

function Get-RegistryInstallLocations {
    param([string]$NamePattern)

    $roots = @(
        "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*",
        "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*",
        "HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*"
    )

    foreach ($root in $roots) {
        $items = Get-ItemProperty $root -ErrorAction SilentlyContinue
        foreach ($item in $items) {
            if ($item.DisplayName -and $item.DisplayName -like $NamePattern -and $item.InstallLocation) {
                $item.InstallLocation
            }
        }
    }
}

function Find-Executable {
    param(
        [string]$ExeName,
        [string[]]$EnvironmentVariables,
        [string[]]$CandidatePaths,
        [string[]]$RegistryNamePatterns,
        [string]$MissingMessage
    )

    foreach ($variable in $EnvironmentVariables) {
        $value = [Environment]::GetEnvironmentVariable($variable, "Process")
        if (-not $value) {
            $value = [Environment]::GetEnvironmentVariable($variable, "User")
        }
        if (-not $value) {
            $value = [Environment]::GetEnvironmentVariable($variable, "Machine")
        }

        $resolved = Test-ExecutablePath $value $ExeName
        if ($resolved) {
            return $resolved
        }
    }

    $fromPath = Get-Command $ExeName -ErrorAction SilentlyContinue
    if ($fromPath) {
        return $fromPath.Source
    }

    foreach ($candidate in $CandidatePaths) {
        $resolved = Test-ExecutablePath $candidate $ExeName
        if ($resolved) {
            return $resolved
        }
    }

    foreach ($pattern in $RegistryNamePatterns) {
        foreach ($installLocation in Get-RegistryInstallLocations $pattern) {
            $resolved = Test-ExecutablePath $installLocation $ExeName
            if ($resolved) {
                return $resolved
            }
        }
    }

    throw $MissingMessage
}

function Find-Nasm {
    $candidates = @(
        (Join-Path $env:ProgramFiles "NASM\nasm.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "NASM\nasm.exe"),
        "C:\NASM\nasm.exe",
        (Join-Path $env:LOCALAPPDATA "Programs\NASM\nasm.exe")
    )

    return Find-Executable `
        -ExeName "nasm.exe" `
        -EnvironmentVariables @("LIQUIDOS_NASM", "NASM_PATH") `
        -CandidatePaths $candidates `
        -RegistryNamePatterns @("*NASM*") `
        -MissingMessage "NASM was not found. If it is installed, set it manually: `$env:LIQUIDOS_NASM='C:\Path\To\nasm.exe'. Download: https://www.nasm.us/pub/nasm/releasebuilds/3.01/win64/nasm-3.01-installer-x64.exe"
}

function Find-Clang {
    $candidates = @(
        (Join-Path $env:ProgramFiles "LLVM\bin\clang.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "LLVM\bin\clang.exe"),
        (Join-Path $env:LOCALAPPDATA "Programs\LLVM\bin\clang.exe")
    )

    return Find-Executable `
        -ExeName "clang.exe" `
        -EnvironmentVariables @("LIQUIDOS_CLANG", "CLANG_PATH", "LIQUIDOS_LLVM_BIN", "LLVM_BIN") `
        -CandidatePaths $candidates `
        -RegistryNamePatterns @("*LLVM*") `
        -MissingMessage "clang.exe was not found. If LLVM is installed, set it manually: `$env:LIQUIDOS_LLVM_BIN='C:\Path\To\LLVM\bin'. Download: https://github.com/llvm/llvm-project/releases/download/llvmorg-22.1.6/LLVM-22.1.6-win64.exe"
}

function Find-Lld {
    $candidates = @(
        (Join-Path $env:ProgramFiles "LLVM\bin\ld.lld.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "LLVM\bin\ld.lld.exe"),
        (Join-Path $env:LOCALAPPDATA "Programs\LLVM\bin\ld.lld.exe")
    )

    return Find-Executable `
        -ExeName "ld.lld.exe" `
        -EnvironmentVariables @("LIQUIDOS_LLD", "LLD_PATH", "LIQUIDOS_LLVM_BIN", "LLVM_BIN") `
        -CandidatePaths $candidates `
        -RegistryNamePatterns @("*LLVM*") `
        -MissingMessage "ld.lld.exe was not found. If LLVM is installed, set it manually: `$env:LIQUIDOS_LLVM_BIN='C:\Path\To\LLVM\bin'. Download: https://github.com/llvm/llvm-project/releases/download/llvmorg-22.1.6/LLVM-22.1.6-win64.exe"
}

function Find-Objcopy {
    $candidates = @(
        (Join-Path $env:ProgramFiles "LLVM\bin\llvm-objcopy.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "LLVM\bin\llvm-objcopy.exe"),
        (Join-Path $env:LOCALAPPDATA "Programs\LLVM\bin\llvm-objcopy.exe")
    )

    return Find-Executable `
        -ExeName "llvm-objcopy.exe" `
        -EnvironmentVariables @("LIQUIDOS_OBJCOPY", "OBJCOPY_PATH", "LIQUIDOS_LLVM_BIN", "LLVM_BIN") `
        -CandidatePaths $candidates `
        -RegistryNamePatterns @("*LLVM*") `
        -MissingMessage "llvm-objcopy.exe was not found. If LLVM is installed, set it manually: `$env:LIQUIDOS_LLVM_BIN='C:\Path\To\LLVM\bin'. Download: https://github.com/llvm/llvm-project/releases/download/llvmorg-22.1.6/LLVM-22.1.6-win64.exe"
}

function Find-VBoxManage {
    $candidates = @(
        (Join-Path $env:ProgramFiles "Oracle\VirtualBox\VBoxManage.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "Oracle\VirtualBox\VBoxManage.exe"),
        (Join-Path $env:ProgramFiles "VirtualBox\VBoxManage.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "VirtualBox\VBoxManage.exe")
    )

    return Find-Executable `
        -ExeName "VBoxManage.exe" `
        -EnvironmentVariables @("LIQUIDOS_VBOXMANAGE", "VBOXMANAGE_PATH") `
        -CandidatePaths $candidates `
        -RegistryNamePatterns @("*VirtualBox*", "*Oracle VM VirtualBox*") `
        -MissingMessage "VBoxManage was not found. If VirtualBox is installed, set it manually: `$env:LIQUIDOS_VBOXMANAGE='C:\Path\To\VBoxManage.exe'. Download: https://download.virtualbox.org/virtualbox/7.2.8/VirtualBox-7.2.8-173730-Win.exe"
}

function Invoke-CheckedCommand {
    param(
        [string]$FilePath,
        [string[]]$ArgumentList
    )

    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        throw "'$FilePath' failed with exit code $LASTEXITCODE."
    }
}
