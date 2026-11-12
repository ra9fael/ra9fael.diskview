param(
  [string]$QtPrefix = 'D:\tools\Qt\6.12.0\msvc2022_64',
  [string]$BuildDir = '',
  [string]$OutputDir = ''
)
$ErrorActionPreference = 'Stop'
$qmlApp = Split-Path $PSScriptRoot -Parent
if (-not $BuildDir) { $BuildDir = Join-Path $qmlApp 'build' }
if (-not $OutputDir) { $OutputDir = Join-Path $BuildDir 'deploy' }

$exe = Join-Path $BuildDir 'bin\diskview-qml.exe'
if (-not (Test-Path $exe)) { throw "Missing launcher: $exe (build it first)" }

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$plugins = Join-Path $OutputDir 'plugins'
$qml = Join-Path $OutputDir 'qml'
New-Item -ItemType Directory -Force -Path $plugins, $qml | Out-Null

$deploy = Join-Path $QtPrefix 'bin\windeployqt.exe'
if (-not (Test-Path $deploy)) { throw "Missing windeployqt: $deploy" }

& $deploy --qmldir (Join-Path $qmlApp 'qml') --dir $OutputDir --plugindir $plugins --qml-deploy-dir $qml `
  --no-translations --no-compiler-runtime --no-opengl-sw --force $exe
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE" }

# The Direct3D 11 backend compiles its shaders through d3dcompiler_47.dll,
# which is not part of Windows and therefore has to travel with the app.
$d3dCompiler = Join-Path $QtPrefix 'bin\d3dcompiler_47.dll'
if (Test-Path $d3dCompiler) {
  Copy-Item $d3dCompiler $OutputDir -Force
}

# windeployqt only ships the platform plugin of the host it runs on. The
# offscreen plugin is what the headless checks use.
$platforms = Join-Path $plugins 'platforms'
New-Item -ItemType Directory -Force -Path $platforms | Out-Null
foreach ($name in 'qoffscreen.dll', 'qminimal.dll') {
  $source = Join-Path $QtPrefix "plugins\platforms\$name"
  if (Test-Path $source) {
    Copy-Item $source $platforms -Force
  }
}

Copy-Item $exe $OutputDir -Force

# The QML module is compiled into the executable, but its build directory is
# still listed so that tooling and the qmldir stay resolvable.
$moduleDir = (Join-Path $BuildDir 'qmlapp') -replace '\\', '/'
$conf = @"
[Paths]
Prefix = .
QmlImports = $moduleDir
"@
Set-Content -Path (Join-Path $OutputDir 'qt.conf') -Value $conf -Encoding ascii

$files = (Get-ChildItem -Path $OutputDir -Recurse -File).Count
"Deployed the standalone launcher to $OutputDir ($files files)"
