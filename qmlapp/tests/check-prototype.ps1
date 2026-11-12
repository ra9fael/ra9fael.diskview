param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$qmlApp = Split-Path $PSScriptRoot -Parent
$repoRoot = Split-Path $qmlApp -Parent
$build = Join-Path $qmlApp 'build'
$deploy = Join-Path $build 'deploy'
$sdk = 'D:\tools\Qt\6.12.0\msvc2022_64'
$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
$exe = Join-Path $deploy 'diskview-qml.exe'
$geometryExe = Join-Path $build 'tests\diskview-chart-geometry-tests.exe'

node (Join-Path $PSScriptRoot 'check-fixture.cjs')
if ($LASTEXITCODE -ne 0) { throw 'Fixture totals are inconsistent' }

if (-not $SkipBuild) {
  & cmd /c "call `"$vcvars`" >nul && cmake -S `"$repoRoot`" -B `"$build`" -G Ninja -DCMAKE_BUILD_TYPE=Release -DDISKVIEW_BUILD_QML=ON -DDISKVIEW_QT_PREFIX=`"$sdk`""
  if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
  & cmd /c "call `"$vcvars`" >nul && cmake --build `"$build`""
  if ($LASTEXITCODE -ne 0) { throw 'CMake build failed' }
}

if (-not (Test-Path $geometryExe)) { throw "Missing chart geometry tests: $geometryExe" }
& $geometryExe
if ($LASTEXITCODE -ne 0) { throw 'Chart geometry unit tests failed' }

& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $qmlApp 'tools\deploy-wrapper.ps1') -QtPrefix $sdk -BuildDir $build -OutputDir $deploy
if ($LASTEXITCODE -ne 0) { throw 'Deployment of the standalone launcher failed' }
if (-not (Test-Path $exe)) { throw "Missing launcher: $exe" }

# The checks run against the deployed copy, with the Qt SDK taken out of the
# PATH: the wrapper has to stand on its own, the way a user would run it.
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_QUICK_BACKEND = 'software'
foreach ($mode in 0,1) {
  $name = if ($mode -eq 0) { 'qml-treemap-native.png' } else { 'qml-sunburst-painted.png' }
  $image = Join-Path $build $name
  Remove-Item $image -ErrorAction SilentlyContinue
  $output = & $exe --view-mode $mode --screenshot $image --self-test 2>&1
  $exitCode = $LASTEXITCODE
  $text = ($output | Out-String)
  if ($exitCode -ne 0) { throw "Launcher failed in view mode ${mode}: $text" }
  $errors = $text -split "`r?`n" | Where-Object { $_ -match 'failed to load|TypeError|ReferenceError|is not defined|QSG.*error' }
  if ($errors) { throw "Runtime error in mode ${mode}: $($errors -join '; ')" }
  $report = ($text -split "`r?`n" | Where-Object { $_ -match "`t" -and $_ -match '(PASS|FAIL)$' }) | Select-Object -First 1
  if (-not $report) { throw "No self-test report in mode ${mode}: $text" }
  $fields = $report -split "`t"
  if ($fields.Count -ne 7 -or $fields[0] -ne "$mode" -or [string]::IsNullOrWhiteSpace($fields[1]) -or $fields[2] -ne 'regions' -or $fields[3] -ne 'transform' -or $fields[4] -ne 'navigate' -or $fields[5] -ne 'return' -or $fields[6] -ne 'PASS') { throw "Self-test failed in mode ${mode}: $report" }
  if (-not (Test-Path $image)) { throw "Missing screenshot $image" }
  $item = Get-Item $image
  if ($item.Length -lt 20000) { throw "Screenshot seems blank: $name ($($item.Length) bytes)" }
  "PASS $name ($($item.Length) bytes), self-test: $($report.Trim())"
}

# A real scan of a small tree, driven through the same scanner the window
# uses: this is the path that has to work before the host plugin is involved.
# The tree is written out first, so it has known directories to enter.
$scanTarget = Join-Path $build 'scan-fixture'
Remove-Item $scanTarget -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path (Join-Path $scanTarget 'alpha\beta'), (Join-Path $scanTarget 'alpha\gamma') | Out-Null
Set-Content -Path (Join-Path $scanTarget 'alpha\beta\one.txt') -Value ('x' * 4096) -Encoding ascii
Set-Content -Path (Join-Path $scanTarget 'alpha\gamma\two.bin') -Value ('y' * 16384) -Encoding ascii
Set-Content -Path (Join-Path $scanTarget 'delta.txt') -Value ('z' * 2048) -Encoding ascii
$scanImage = Join-Path $build 'qml-scan-real.png'
Remove-Item $scanImage -ErrorAction SilentlyContinue
$output = & $exe --scan $scanTarget --view-mode 0 --screenshot $scanImage --self-test 2>&1
$exitCode = $LASTEXITCODE
$text = ($output | Out-String)
if ($exitCode -ne 0) { throw "Launcher failed on the real scan: $text" }
$errors = $text -split "`r?`n" | Where-Object { $_ -match 'failed to load|TypeError|ReferenceError|is not defined|QSG.*error' }
if ($errors) { throw "Runtime error on the real scan: $($errors -join '; ')" }
$report = ($text -split "`r?`n" | Where-Object { $_ -match "`t" -and $_ -match '(PASS|FAIL)$' }) | Select-Object -First 1
if (-not $report) { throw "No self-test report on the real scan: $text" }
$fields = $report -split "`t"
if ($fields.Count -ne 7 -or $fields[6] -ne 'PASS') { throw "Self-test failed on the real scan: $report" }
if (-not (Test-Path $scanImage)) { throw "Missing screenshot $scanImage" }
$item = Get-Item $scanImage
if ($item.Length -lt 20000) { throw "Scan screenshot seems blank: $($item.Length) bytes" }
"PASS qml-scan-real.png ($($item.Length) bytes), self-test: $($report.Trim())"
