param(
    [string]$VmName = "LiquidOS",
    [switch]$NoBuild,
    [switch]$NoStart
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\common.ps1"

$buildDir = Get-BuildDir

if (-not $NoBuild) {
    & "$PSScriptRoot\make-iso.ps1"
    if ($LASTEXITCODE -ne 0) {
        throw "ISO creation failed."
    }
}

$isoPath = Join-Path $buildDir "liquidos.iso"
$diskPath = Join-Path $buildDir "liquidos-data.vdi"
$serialLog = Join-Path $buildDir "serial.log"
if (-not (Test-Path $isoPath)) {
    throw "Missing $isoPath. Run scripts\make-iso.ps1 first."
}

$vbox = Find-VBoxManage

Write-Step "Creating or updating VirtualBox VM '$VmName'"

$registeredVms = & $vbox list vms
$vmExists = $false
foreach ($line in $registeredVms) {
    if ($line -like "`"$VmName`" *") {
        $vmExists = $true
        break
    }
}

if (-not $vmExists) {
    Invoke-CheckedCommand $vbox @("createvm", "--name", $VmName, "--ostype", "Other_64", "--register")
}

Invoke-CheckedCommand $vbox @(
    "modifyvm", $VmName,
    "--memory", "512",
    "--cpus", "1",
    "--firmware", "bios",
    "--boot1", "dvd",
    "--boot2", "none",
    "--boot3", "none",
    "--boot4", "none",
    "--vram", "128",
    "--graphicscontroller", "vboxvga",
    "--mouse", "ps2",
    "--usb", "off",
    "--nic1", "none",
    "--uart1", "0x3F8", "4",
    "--uartmode1", "file", $serialLog
)

Invoke-CheckedCommand $vbox @("setextradata", $VmName, "CustomVideoMode1", "1920x1080x32")
Invoke-CheckedCommand $vbox @("setextradata", $VmName, "CustomVideoMode2", "1680x1050x32")
Invoke-CheckedCommand $vbox @("setextradata", $VmName, "CustomVideoMode3", "1440x900x32")
Invoke-CheckedCommand $vbox @("setextradata", $VmName, "CustomVideoMode4", "1280x800x32")

$storageInfo = & $vbox showvminfo $VmName --machinereadable
$hasIdeController = $false
foreach ($line in $storageInfo) {
    if ($line -like 'storagecontrollername*="IDE Controller"') {
        $hasIdeController = $true
        break
    }
}

if (-not $hasIdeController) {
    Invoke-CheckedCommand $vbox @("storagectl", $VmName, "--name", "IDE Controller", "--add", "ide", "--controller", "PIIX4")
}

if (-not (Test-Path $diskPath)) {
    Write-Step "Creating LiquidOS writable data disk"
    Invoke-CheckedCommand $vbox @("createmedium", "disk", "--filename", $diskPath, "--size", "16", "--format", "VDI")
}

Invoke-CheckedCommand $vbox @(
    "storageattach", $VmName,
    "--storagectl", "IDE Controller",
    "--port", "0",
    "--device", "0",
    "--type", "hdd",
    "--medium", $diskPath
)

Invoke-CheckedCommand $vbox @(
    "storageattach", $VmName,
    "--storagectl", "IDE Controller",
    "--port", "1",
    "--device", "0",
    "--type", "dvddrive",
    "--medium", $isoPath
)

Write-Host ""
Write-Host "VirtualBox VM is configured." -ForegroundColor Green
Write-Host "ISO attached: $isoPath"

if (-not $NoStart) {
    Write-Step "Starting VirtualBox VM"
    Invoke-CheckedCommand $vbox @("startvm", $VmName, "--type", "gui")
}
else {
    Write-Host "NoStart was set, so the VM was not launched."
}
