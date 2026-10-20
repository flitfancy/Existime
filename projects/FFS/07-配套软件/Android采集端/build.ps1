param(
    [string]$Gradle = 'gradle',
    [switch]$TestOnly,
    [string]$BuildWorkspace = (Join-Path ([System.IO.Path]::GetTempPath()) 'Existime-Android')
)
$ErrorActionPreference = 'Stop'
$stage = Join-Path $BuildWorkspace ([Guid]::NewGuid().ToString('N'))
if ($stage -match '[^\x00-\x7F]') {
    throw 'Android build workspace must use an ASCII path. Set -BuildWorkspace, for example C:\Build\Existime-Android.'
}
New-Item -ItemType Directory -Path $stage -Force | Out-Null
# Copy source only into a fresh workspace; do not delete or mirror either tree.
& robocopy $PSScriptRoot $stage /E /NFL /NDL /NJH /NJS /XD .gradle .kotlin build build-output .git /XF local.properties | Out-Null
if ($LASTEXITCODE -ge 8) { throw "Android source copy failed: $LASTEXITCODE" }
$tasks = @('testDebugUnitTest')
if (-not $TestOnly) { $tasks += 'assembleDebug' }
Write-Host "Android source workspace: $stage"
& $Gradle -p $stage --no-daemon @tasks
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$outputs = Join-Path $PSScriptRoot 'build-output'
New-Item -ItemType Directory -Path $outputs -Force | Out-Null
foreach ($relativePath in @('app\build\outputs\apk\debug', 'app\build\test-results\testDebugUnitTest')) {
    $source = Join-Path $stage $relativePath
    if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $outputs -Recurse -Force }
}
Write-Host "Build outputs: $outputs"
