param([string]$BuildDir = 'qmlapp/build')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$build = Join-Path $root $BuildDir
$exe = Join-Path $build 'bin/diskview-qml.exe'
$output = Join-Path $root 'build/dock-validation'
New-Item -ItemType Directory -Force $output | Out-Null
$keys = @('WS_DATA_DIR','QT_QPA_PLATFORM','QT_QUICK_BACKEND','QT_SCALE_FACTOR','QT_FORCE_STDERR_LOGGING')
$saved = @{}
foreach ($key in $keys) { $saved[$key] = [Environment]::GetEnvironmentVariable($key) }
try {
    $env:QT_QPA_PLATFORM = 'offscreen'
    $env:QT_QUICK_BACKEND = 'software'
    $env:QT_FORCE_STDERR_LOGGING = '1'
    foreach ($scale in '1','1.25','1.5','1.75') {
        foreach ($mode in 0,1) {
            $data = Join-Path $output "data-$scale-$mode"
            New-Item -ItemType Directory -Force $data | Out-Null
            $settings = Join-Path $data 'diskview-qml.json'
            '{"chartLayout":"dock","chartDepth":3}' | Set-Content $settings -Encoding utf8
            $env:WS_DATA_DIR = $data
            $env:QT_SCALE_FACTOR = $scale
            $image = Join-Path $output "dock-$scale-$mode.png"
            $text = (& $exe --view-mode $mode --self-test --screenshot $image --delay 1000 2>&1 | Out-String)
            if ($LASTEXITCODE -ne 0 -or $text -notmatch 'PASS' -or $text -match 'TypeError|ReferenceError|Unable to assign') {
                throw "Dock interaction failed at $scale, mode $mode`: $text"
            }
            if ((Get-Item $image).Length -lt 20000) { throw "Blank dock screenshot: $image" }
            $json = Get-Content $settings -Raw | ConvertFrom-Json
            if (-not $json.dockLayout) { throw 'Dock layout was not persisted' }
            $text = (& $exe --view-mode $mode --self-test --screenshot $image --delay 1000 2>&1 | Out-String)
            if ($LASTEXITCODE -ne 0 -or $text -notmatch 'PASS') { throw "Dock restart failed: $text" }
            "PASS Dock $scale mode=$mode`: pointer merge/split/resize, fullscreen, restart"
        }
    }
} finally {
    foreach ($key in $keys) { [Environment]::SetEnvironmentVariable($key, $saved[$key]) }
}
