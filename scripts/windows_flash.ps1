param(
    [string]$Port = "COM11",
    [int]$Baud = 460800,
    [string]$BuildDir = "$PSScriptRoot\..\.pio\build\lilygo_t_embed_cc1101"
)

$ErrorActionPreference = "Stop"

function Resolve-BuildPath([string]$Name) {
    $path = Join-Path $BuildDir $Name
    if (-not (Test-Path $path)) {
        throw "Missing build artifact: $path. Run 'pio run' first."
    }
    return (Resolve-Path $path).Path
}

Write-Host "Probing $Port before writing..."
$probe = & py -3 -m esptool --chip auto --port $Port chip-id 2>&1
$probe | Write-Host
if ($LASTEXITCODE -ne 0) {
    throw "esptool probe failed on $Port. Put the T-Embed into ESP32-S3 bootloader mode and retry."
}
if (($probe -join "`n") -notmatch "ESP32-S3") {
    throw "Refusing to flash: connected chip is not ESP32-S3."
}

$bootloader = Resolve-BuildPath "bootloader.bin"
$partitions = Resolve-BuildPath "partitions.bin"
$firmware = Resolve-BuildPath "firmware.bin"

Write-Host "Writing ESP32-S3 image to $Port..."
& py -3 -m esptool `
    --chip esp32s3 `
    --port $Port `
    --baud $Baud `
    write-flash `
    --flash-mode dio `
    --flash-freq 80m `
    --flash-size 16MB `
    0x0 $bootloader `
    0x8000 $partitions `
    0x10000 $firmware
if ($LASTEXITCODE -ne 0) {
    throw "Flash failed."
}

Write-Host "Flash completed. If serial does not reappear, unplug/replug the board."
