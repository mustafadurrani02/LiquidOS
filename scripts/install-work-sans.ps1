$ErrorActionPreference = "Stop"
. "$PSScriptRoot\common.ps1"

$root = Get-ProjectRoot
$fontDir = Join-Path $root "assets\fonts"
$fontPath = Join-Path $fontDir "WorkSans[wght].ttf"
$url = "https://github.com/google/fonts/raw/main/ofl/worksans/WorkSans%5Bwght%5D.ttf"

New-Item -ItemType Directory -Force -Path $fontDir | Out-Null

Write-Step "Downloading Work Sans"
$ProgressPreference = "SilentlyContinue"
Invoke-WebRequest -Uri $url -OutFile $fontPath -UseBasicParsing

Write-Step "Generating LiquidOS UI font from Work Sans"
& "$PSScriptRoot\generate-ui-font.ps1" -FontPath $fontPath

Write-Host ""
Write-Host "Work Sans is ready. Rebuild LiquidOS with scripts\run-virtualbox.ps1." -ForegroundColor Green
