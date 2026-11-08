# Render the real window paint path with deterministic test data. No visible
# window, host installation, Qt runtime, or full-drive scan is required.
param([string]$OutputDirectory = "")
$ErrorActionPreference = "Stop"
$pluginRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$testBinary = Join-Path $pluginRoot 'build\tests\diskview_tests.exe'
if (-not (Test-Path -LiteralPath $testBinary)) {
    throw 'Build the project first: cmake --build build --config Release'
}
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $pluginRoot 'build\verify'
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
$previousOutput = $env:DISKVIEW_SNAPSHOT_DIR
try {
    $env:DISKVIEW_SNAPSHOT_DIR = $OutputDirectory
    & $testBinary
    if ($LASTEXITCODE -ne 0) {
        throw "Window regression tests failed (exit $LASTEXITCODE)"
    }
} finally {
    $env:DISKVIEW_SNAPSHOT_DIR = $previousOutput
}
Add-Type -AssemblyName System.Drawing
Get-ChildItem -LiteralPath $OutputDirectory -Filter '*.bmp' | ForEach-Object {
    $snapshot = [System.Drawing.Image]::FromFile($_.FullName)
    try {
        $pngPath = [System.IO.Path]::ChangeExtension($_.FullName, '.png')
        $snapshot.Save($pngPath, [System.Drawing.Imaging.ImageFormat]::Png)
        Write-Output $pngPath
    } finally {
        $snapshot.Dispose()
    }
}
