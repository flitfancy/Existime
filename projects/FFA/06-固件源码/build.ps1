param(
    [string]$ArduinoCli = 'arduino-cli',
    [string]$ConfigFile = '',
    [string]$BuildPath = (Join-Path $PSScriptRoot 'build-wifi-transport')
)
$ErrorActionPreference = 'Stop'
$compileArgs = @()
if ($ConfigFile) { $compileArgs += @('--config-file', $ConfigFile) }
$compileArgs += @('compile', '--fqbn', 'esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashMode=dio,FlashSize=16M,PartitionScheme=custom,PSRAM=opi', '--build-path', $BuildPath, (Join-Path $PSScriptRoot 'firmware-wifi-transport'))
& $ArduinoCli @compileArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
