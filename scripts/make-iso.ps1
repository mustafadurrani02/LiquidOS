param(
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\common.ps1"

function Write-Ascii {
    param([byte[]]$Buffer, [int]$Offset, [int]$Length, [string]$Text)
    for ($i = 0; $i -lt $Length; $i++) {
        $Buffer[$Offset + $i] = 0x20
    }
    $bytes = [System.Text.Encoding]::ASCII.GetBytes($Text)
    $copyLength = [Math]::Min($Length, $bytes.Length)
    [System.Array]::Copy($bytes, 0, $Buffer, $Offset, $copyLength)
}

function Write-AsciiZeroPadded {
    param([byte[]]$Buffer, [int]$Offset, [int]$Length, [string]$Text)
    for ($i = 0; $i -lt $Length; $i++) {
        $Buffer[$Offset + $i] = 0
    }
    $bytes = [System.Text.Encoding]::ASCII.GetBytes($Text)
    $copyLength = [Math]::Min($Length, $bytes.Length)
    [System.Array]::Copy($bytes, 0, $Buffer, $Offset, $copyLength)
}

function Write-U16LE {
    param([byte[]]$Buffer, [int]$Offset, [int]$Value)
    $Buffer[$Offset] = [byte]($Value -band 0xFF)
    $Buffer[$Offset + 1] = [byte](($Value -shr 8) -band 0xFF)
}

function Write-U16BE {
    param([byte[]]$Buffer, [int]$Offset, [int]$Value)
    $Buffer[$Offset] = [byte](($Value -shr 8) -band 0xFF)
    $Buffer[$Offset + 1] = [byte]($Value -band 0xFF)
}

function Write-U32LE {
    param([byte[]]$Buffer, [int]$Offset, [int64]$Value)
    $Buffer[$Offset] = [byte]($Value -band 0xFF)
    $Buffer[$Offset + 1] = [byte](($Value -shr 8) -band 0xFF)
    $Buffer[$Offset + 2] = [byte](($Value -shr 16) -band 0xFF)
    $Buffer[$Offset + 3] = [byte](($Value -shr 24) -band 0xFF)
}

function Write-U32BE {
    param([byte[]]$Buffer, [int]$Offset, [int64]$Value)
    $Buffer[$Offset] = [byte](($Value -shr 24) -band 0xFF)
    $Buffer[$Offset + 1] = [byte](($Value -shr 16) -band 0xFF)
    $Buffer[$Offset + 2] = [byte](($Value -shr 8) -band 0xFF)
    $Buffer[$Offset + 3] = [byte]($Value -band 0xFF)
}

function Write-BothEndianU16 {
    param([byte[]]$Buffer, [int]$Offset, [int]$Value)
    Write-U16LE $Buffer $Offset $Value
    Write-U16BE $Buffer ($Offset + 2) $Value
}

function Write-BothEndianU32 {
    param([byte[]]$Buffer, [int]$Offset, [int64]$Value)
    Write-U32LE $Buffer $Offset $Value
    Write-U32BE $Buffer ($Offset + 4) $Value
}

function Write-DirectoryRecord {
    param(
        [byte[]]$Buffer,
        [int]$Offset,
        [int]$Extent,
        [int]$DataLength,
        [byte]$Flags,
        [byte]$Identifier
    )

    $Buffer[$Offset] = 34
    $Buffer[$Offset + 1] = 0
    Write-BothEndianU32 $Buffer ($Offset + 2) $Extent
    Write-BothEndianU32 $Buffer ($Offset + 10) $DataLength
    $Buffer[$Offset + 18] = 126
    $Buffer[$Offset + 19] = 5
    $Buffer[$Offset + 20] = 31
    $Buffer[$Offset + 21] = 0
    $Buffer[$Offset + 22] = 0
    $Buffer[$Offset + 23] = 0
    $Buffer[$Offset + 24] = 0
    $Buffer[$Offset + 25] = $Flags
    $Buffer[$Offset + 26] = 0
    $Buffer[$Offset + 27] = 0
    Write-BothEndianU16 $Buffer ($Offset + 28) 1
    $Buffer[$Offset + 32] = 1
    $Buffer[$Offset + 33] = $Identifier
}

$root = Get-ProjectRoot
$buildDir = Get-BuildDir

if (-not $NoBuild) {
    & "$PSScriptRoot\make-image.ps1"
    if ($LASTEXITCODE -ne 0) {
        throw "Boot image creation failed."
    }
}

$bootImagePath = Join-Path $buildDir "liquidos.img"
$isoPath = Join-Path $buildDir "liquidos.iso"

if (-not (Test-Path $bootImagePath)) {
    throw "Missing $bootImagePath. Run scripts\make-image.ps1 first."
}

Write-Step "Creating bootable El Torito ISO"

$sectorSize = 2048
$pathTableLba = 19
$pathTableMLba = 20
$rootDirLba = 21
$bootCatalogLba = 22
$bootImageLba = 23

$bootImage = [System.IO.File]::ReadAllBytes($bootImagePath)
[int]$bootImageSectors = [Math]::Ceiling($bootImage.Length / $sectorSize)
[int]$totalSectors = $bootImageLba + $bootImageSectors
$iso = New-Object byte[] ($totalSectors * $sectorSize)

# Primary Volume Descriptor
$pvd = 16 * $sectorSize
$iso[$pvd] = 1
Write-Ascii $iso ($pvd + 1) 5 "CD001"
$iso[$pvd + 6] = 1
Write-Ascii $iso ($pvd + 8) 32 "LIQUIDOS"
Write-Ascii $iso ($pvd + 40) 32 "LIQUIDOS"
Write-BothEndianU32 $iso ($pvd + 80) $totalSectors
Write-BothEndianU16 $iso ($pvd + 120) 1
Write-BothEndianU16 $iso ($pvd + 124) 1
Write-BothEndianU16 $iso ($pvd + 128) $sectorSize
Write-BothEndianU32 $iso ($pvd + 132) 10
Write-U32LE $iso ($pvd + 140) $pathTableLba
Write-U32LE $iso ($pvd + 144) 0
Write-U32BE $iso ($pvd + 148) $pathTableMLba
Write-U32BE $iso ($pvd + 152) 0
Write-DirectoryRecord $iso ($pvd + 156) $rootDirLba $sectorSize 2 0
Write-Ascii $iso ($pvd + 190) 128 "LIQUIDOS"
Write-Ascii $iso ($pvd + 318) 128 "LIQUIDOS_BOOTLOADER"
Write-Ascii $iso ($pvd + 446) 128 "OPENAI_CODEX"
Write-Ascii $iso ($pvd + 574) 128 "LIQUIDOS"
$iso[$pvd + 881] = 1

# El Torito Boot Record Volume Descriptor
$brvd = 17 * $sectorSize
$iso[$brvd] = 0
Write-Ascii $iso ($brvd + 1) 5 "CD001"
$iso[$brvd + 6] = 1
Write-AsciiZeroPadded $iso ($brvd + 7) 32 "EL TORITO SPECIFICATION"
Write-AsciiZeroPadded $iso ($brvd + 39) 32 "LIQUIDOS"
Write-U32LE $iso ($brvd + 71) $bootCatalogLba

# Volume Descriptor Set Terminator
$term = 18 * $sectorSize
$iso[$term] = 255
Write-Ascii $iso ($term + 1) 5 "CD001"
$iso[$term + 6] = 1

# Little-endian root path table
$ptl = $pathTableLba * $sectorSize
$iso[$ptl] = 1
$iso[$ptl + 1] = 0
Write-U32LE $iso ($ptl + 2) $rootDirLba
Write-U16LE $iso ($ptl + 6) 1
$iso[$ptl + 8] = 0
$iso[$ptl + 9] = 0

# Big-endian root path table
$ptm = $pathTableMLba * $sectorSize
$iso[$ptm] = 1
$iso[$ptm + 1] = 0
Write-U32BE $iso ($ptm + 2) $rootDirLba
Write-U16BE $iso ($ptm + 6) 1
$iso[$ptm + 8] = 0
$iso[$ptm + 9] = 0

# Root directory with "." and ".." records.
$rootDir = $rootDirLba * $sectorSize
Write-DirectoryRecord $iso $rootDir $rootDirLba $sectorSize 2 0
Write-DirectoryRecord $iso ($rootDir + 34) $rootDirLba $sectorSize 2 1

# El Torito boot catalog.
$catalog = $bootCatalogLba * $sectorSize
$validation = New-Object byte[] 32
$validation[0] = 1
$validation[1] = 0
$id = [System.Text.Encoding]::ASCII.GetBytes("LiquidOS")
[System.Array]::Copy($id, 0, $validation, 4, $id.Length)
$validation[30] = 0x55
$validation[31] = 0xAA

$sum = 0
for ($i = 0; $i -lt 16; $i++) {
    $word = $validation[$i * 2] -bor ($validation[$i * 2 + 1] -shl 8)
    $sum = ($sum + $word) -band 0xFFFF
}
$checksum = ((0 - $sum) -band 0xFFFF)
$validation[28] = [byte]($checksum -band 0xFF)
$validation[29] = [byte](($checksum -shr 8) -band 0xFF)
[System.Array]::Copy($validation, 0, $iso, $catalog, 32)

$initialEntry = $catalog + 32
$iso[$initialEntry] = 0x88
$iso[$initialEntry + 1] = 0x02
Write-U16LE $iso ($initialEntry + 2) 0
$iso[$initialEntry + 4] = 0
$iso[$initialEntry + 5] = 0
Write-U16LE $iso ($initialEntry + 6) 1
Write-U32LE $iso ($initialEntry + 8) $bootImageLba

[System.Array]::Copy($bootImage, 0, $iso, ($bootImageLba * $sectorSize), $bootImage.Length)
[System.IO.File]::WriteAllBytes($isoPath, $iso)

Write-Host ""
Write-Host "ISO created." -ForegroundColor Green
Write-Host $isoPath
