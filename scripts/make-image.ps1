param(
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\common.ps1"

$root = Get-ProjectRoot
$buildDir = Get-BuildDir

if (-not $NoBuild) {
    & "$PSScriptRoot\build.ps1"
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed."
    }
}

$stage1Bin = Join-Path $buildDir "stage1.bin"
$stage2Padded = Join-Path $buildDir "stage2.pad.bin"
$kernelPadded = Join-Path $buildDir "kernel.pad.bin"
$imagePath = Join-Path $buildDir "liquidos.img"

if (-not (Test-Path $stage1Bin)) {
    throw "Missing $stage1Bin. Run scripts\build.ps1 first."
}

if (-not (Test-Path $stage2Padded)) {
    throw "Missing $stage2Padded. Run scripts\build.ps1 first."
}

if (-not (Test-Path $kernelPadded)) {
    throw "Missing $kernelPadded. Run scripts\build.ps1 first."
}

Write-Step "Creating 1.44 MB boot floppy image"

$imageSize = 1440 * 1024
$image = New-Object byte[] $imageSize
$stage1 = [System.IO.File]::ReadAllBytes($stage1Bin)
$stage2 = [System.IO.File]::ReadAllBytes($stage2Padded)
$kernel = [System.IO.File]::ReadAllBytes($kernelPadded)

$kernelOffset = 512 + $stage2.Length
if ($kernelOffset + $kernel.Length -gt $image.Length) {
    throw "The boot image is too small for this kernel. Kernel end would be $($kernelOffset + $kernel.Length), image size is $($image.Length)."
}

[System.Array]::Copy($stage1, 0, $image, 0, $stage1.Length)
[System.Array]::Copy($stage2, 0, $image, 512, $stage2.Length)
[System.Array]::Copy($kernel, 0, $image, $kernelOffset, $kernel.Length)

[System.IO.File]::WriteAllBytes($imagePath, $image)

Write-Host ""
Write-Host "Boot image created." -ForegroundColor Green
Write-Host $imagePath
