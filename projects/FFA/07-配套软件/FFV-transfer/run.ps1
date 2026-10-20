param([string]$Python = '', [switch]$Check)
$ErrorActionPreference = 'Stop'
if (-not $Python) {
    $venvPython = Join-Path $PSScriptRoot '.venv\Scripts\python.exe'
    $Python = if (Test-Path -LiteralPath $venvPython) { $venvPython } else { 'python' }
}
. (Join-Path $PSScriptRoot 'settings.example.ps1')
$localSettings = Join-Path $PSScriptRoot 'settings.local.ps1'
if (Test-Path -LiteralPath $localSettings) { . $localSettings }
Push-Location $PSScriptRoot
try {
    if ($Check) { & $Python 'run_service.py' '--check' }
    else { & $Python 'run_service.py' }
    $serviceExitCode = $LASTEXITCODE
} finally { Pop-Location }
exit $serviceExitCode
