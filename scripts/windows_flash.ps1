# ⬡ CK42X · ck42x.com
# windows_flash: probe-then-flash esp32-s3 firmware to a t-embed over a windows com port
# gold on black. sting once.
# guarantees: probe the chip first, then flash bootloader, partitions, and firmware to an esp32-s3.
# rejects: throws on a failed probe, a non-esp32-s3 chip, a missing artifact, or a nonzero esptool exit.
param(
    [string]$Port = "COM11",
    [int]$Baud = 460800,
    [string]$BuildDir = "$PSScriptRoot\..\.pio\build\lilygo_t_embed_cc1101"
)

# WHY: stop on the first cmdlet error; native esptool exits are checked via $LASTEXITCODE below.
$ErrorActionPreference = "Stop"

# guarantees: a resolved absolute path to an existing artifact under $BuildDir.
# rejects: throws naming the missing artifact when it has not been built.
function Resolve-BuildPath([string]$Name) {
    $path = Join-Path $BuildDir $Name
    if (-not (Test-Path $path)) {
        throw "Missing build artifact: $path. Run 'pio run' first."
    }
    return (Resolve-Path $path).Path
}

# SAFETY: refuse to flash unless esptool's chip-id reports ESP32-S3 on $Port.
# TRAP: the gate is a text match over probe output, so esptool wording changes can break it.
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
# INVARIANT: esp32-s3 boot map 0x0 / 0x8000 / 0x10000; flash geometry dio, 80m, 16MB.
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
