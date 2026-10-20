param(
    [string]$Python = '',
    [string]$BuildWorkspace = (Join-Path ([System.IO.Path]::GetTempPath()) 'Existime-FFS'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'build-output'),
    [switch]$SafeEsptool
)
$ErrorActionPreference = 'Stop'
if (-not $Python) {
    $Python = (& py -3 -c 'import sys; print(sys.executable)').Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Specify -Python with a Python interpreter that has PlatformIO installed.' }
}
if ($BuildWorkspace -match '[^\x00-\x7F]') {
    throw 'FFS build workspace must use an ASCII path. Set -BuildWorkspace, for example C:\Build\Existime-FFS.'
}
$stage = Join-Path $BuildWorkspace ([Guid]::NewGuid().ToString('N'))
$buildArguments = @((Join-Path $PSScriptRoot 'scripts\build_ota.py'), '--pio-python', $Python, '--stage', $stage, '--output-dir', $OutputDirectory)
if ($SafeEsptool) { $buildArguments += '--safe-esptool' }
& $Python @buildArguments
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
