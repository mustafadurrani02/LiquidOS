$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
Set-Location $projectRoot

$gitCandidates = @(
    "C:\Program Files\Git\cmd\git.exe",
    "C:\Program Files\Git\bin\git.exe",
    "$env:LOCALAPPDATA\Programs\Git\cmd\git.exe"
)

$git = $gitCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $git) {
    throw "Git was not found. Close PowerShell, open it again, and retry."
}

$safeRoot = $projectRoot.Replace("\", "/")

function Invoke-Git {
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & $git -c "safe.directory=$safeRoot" @args
    $exitCode = $LASTEXITCODE
    $ErrorActionPreference = $previousPreference
    if ($exitCode -ne 0) {
        throw "Git command failed: git $($args -join ' ')"
    }
}

if (-not (Test-Path -LiteralPath ".git")) {
    & $git init -b main
    if ($LASTEXITCODE -ne 0) {
        throw "Git could not initialize the LiquidOS repository."
    }
}

Invoke-Git config user.name "Mustafa Durrani"
Invoke-Git config user.email "mustafadurrani02@gmail.com"

$remoteNames = @(Invoke-Git remote)
if ($remoteNames -notcontains "origin") {
    Invoke-Git remote add origin "https://github.com/mustafadurrani02/LiquidOS.git"
} else {
    Invoke-Git remote set-url origin "https://github.com/mustafadurrani02/LiquidOS.git"
}

Invoke-Git add -A

& $git -c "safe.directory=$safeRoot" diff --cached --quiet
$hasChanges = $LASTEXITCODE -ne 0
if ($hasChanges) {
    Invoke-Git commit -m "Initial LiquidOS source release"
}

Invoke-Git branch -M main
Invoke-Git push -u origin main

Write-Host ""
Write-Host "LiquidOS is now published:" -ForegroundColor Green
Write-Host "https://github.com/mustafadurrani02/LiquidOS"
