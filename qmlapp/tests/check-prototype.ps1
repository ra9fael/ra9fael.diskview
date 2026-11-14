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
$settingsExe = Join-Path $build 'tests\diskview-settings-tests.exe'
$scanExe = Join-Path $build 'tests\diskview-scan-tests.exe'
$modelExe = Join-Path $build 'tests\diskview-model-tests.exe'

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

# The tests write into the build directory, not into the system temp folder,
# and need Qt on the PATH.
if (-not (Test-Path $settingsExe)) { throw "Missing settings tests: $settingsExe" }
if (-not (Test-Path $scanExe)) { throw "Missing scan tests: $scanExe" }
if (-not (Test-Path $modelExe)) { throw "Missing list model tests: $modelExe" }
$savedPath = $env:PATH
$env:PATH = "$sdk\bin;$env:PATH"
Push-Location $build
try {
  & $settingsExe
  $settingsExit = $LASTEXITCODE
  & $scanExe
  $scanExit = $LASTEXITCODE
  & $modelExe
  $modelExit = $LASTEXITCODE
} finally {
  Pop-Location
  $env:PATH = $savedPath
}
if ($settingsExit -ne 0) { throw 'Scan settings unit tests failed' }
if ($scanExit -ne 0) { throw 'Scan behaviour unit tests failed' }
if ($modelExit -ne 0) { throw 'List model unit tests failed' }

& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $qmlApp 'tools\deploy-wrapper.ps1') -QtPrefix $sdk -BuildDir $build -OutputDir $deploy
if ($LASTEXITCODE -ne 0) { throw 'Deployment of the standalone launcher failed' }
if (-not (Test-Path $exe)) { throw "Missing launcher: $exe" }

# The checks run against the deployed copy, with the Qt SDK taken out of the
# PATH: the wrapper has to stand on its own, the way a user would run it.
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_QUICK_BACKEND = 'software'
# The scans run against a settings file of their own: the checks must not read
# the settings of the user, and they must never ask for administrator rights.
$scanData = Join-Path $build 'check-data'
Remove-Item $scanData -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $scanData | Out-Null
'{ "scanMode": 2 }' | Set-Content -Path (Join-Path $scanData 'diskview-qml.json') -Encoding utf8
$env:WS_DATA_DIR = $scanData
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
  if ($fields.Count -ne 10 -or $fields[0] -ne "$mode" -or [string]::IsNullOrWhiteSpace($fields[1]) -or $fields[2] -ne 'regions' -or $fields[3] -ne 'transform' -or $fields[4] -ne 'navigate' -or $fields[5] -ne 'return' -or $fields[6] -notmatch '^list=' -or $fields[7] -ne 'drag' -or $fields[8] -notmatch '^chart=' -or $fields[9] -ne 'PASS') { throw "Self-test failed in mode ${mode}: $report" }
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
if ($fields.Count -ne 10 -or $fields[9] -ne 'PASS') { throw "Self-test failed on the real scan: $report" }
if ($fields[7] -ne 'drag') { throw "The chart kept dragging after the button was released: $report" }
if ($fields[8] -notmatch '^chart=double') { throw "The chart did not enter a folder on a double click: $report" }
if ($fields[6] -notmatch '^list=' -or $fields[6] -notmatch 'view') { throw "The list panel checks did not run, or the view did not follow the model: $report" }
if (-not (Test-Path $scanImage)) { throw "Missing screenshot $scanImage" }
$item = Get-Item $scanImage
if ($item.Length -lt 20000) { throw "Scan screenshot seems blank: $($item.Length) bytes" }
# The scan that ran was held once and then let go: the self-test says so.
if ($text -notmatch 'pause=verified') { throw "The scan was not held and resumed: $text" }
"PASS qml-scan-real.png ($($item.Length) bytes), self-test: $($report.Trim())"

# The held state: the scan waits until it is let go, and nothing is read while
# it waits.
$pauseImage = Join-Path $build 'qml-scan-paused.png'
Remove-Item $pauseImage -ErrorAction SilentlyContinue
$output = & $exe --scan $scanTarget --pause --screenshot $pauseImage --delay 1500 2>&1
$exitCode = $LASTEXITCODE
$text = ($output | Out-String)
if ($exitCode -ne 0) { throw "Launcher failed on the held scan: $text" }
$status = ($text -split "`r?`n" | Where-Object { $_ -match '^scan\s' }) | Select-Object -First 1
if (-not $status) { throw "No scan status line on the held scan: $text" }
if ($status -notmatch 'state=paused') { throw "The scan was not held: $status" }
if (-not (Test-Path $pauseImage)) { throw "Missing screenshot $pauseImage" }
"PASS qml-scan-paused.png, status: $($status.Trim())"

# The fast scan no longer starts a helper or shows a prompt of its own: the
# process either may read the MFT (an elevated Win顺 runner's plugin) or the
# scan walks the tree the ordinary way and says so. A status line therefore
# always carries the elevation state, and a fixture directory is never MFT
# eligible, so the fast flag stays off here no matter how the test runs.
$fastData = Join-Path $build 'check-data-fast'
Remove-Item $fastData -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $fastData | Out-Null
'{ "scanMode": 0 }' | Set-Content -Path (Join-Path $fastData 'diskview-qml.json') -Encoding utf8
$env:WS_DATA_DIR = $fastData
$output = & $exe --scan $scanTarget --screenshot (Join-Path $build 'qml-scan-fast-fallback.png') --delay 1200 2>&1
$exitCode = $LASTEXITCODE
$text = ($output | Out-String)
if ($exitCode -ne 0) { throw "Launcher failed on the fast-scan check: $text" }
$errors = $text -split "`r?`n" | Where-Object { $_ -match 'failed to load|TypeError|ReferenceError|is not defined|QSG.*error' }
if ($errors) { throw "Runtime error on the fast-scan check: $($errors -join '; ')" }
$status = ($text -split "`r?`n" | Where-Object { $_ -match '^scan\s' }) | Select-Object -First 1
if (-not $status) { throw "No scan status line on the fast-scan check: $text" }
if ($status -notmatch 'state=done') { throw "The fast-scan check did not finish: $status" }
if ($status -notmatch 'elevated=(true|false)') { throw "The status line does not say whether the process may read the MFT: $status" }
if ($status -match 'fast=true') { throw "A fixture scan claimed to be a fast scan: $status" }
"PASS fast-scan fallback, status: $($status.Trim())"

# The fullscreen layer: a maximized pane is drawn in a layer of its own, over
# the split, so no other panel can cover it. --maximize drives the window's
# own function, the same one the toolbar button calls.
foreach ($group in 'chart','tables') {
    $image = Join-Path $build "qml-fullscreen-$group.png"
    Remove-Item $image -ErrorAction SilentlyContinue
    $env:WS_DATA_DIR = $scanData
    $output = & $exe --maximize $group --screenshot $image --delay 900 2>&1
    $exitCode = $LASTEXITCODE
    $text = ($output | Out-String)
    if ($exitCode -ne 0) { throw "Launcher failed on the $group fullscreen: $text" }
    if (-not (Test-Path $image)) { throw "Missing fullscreen screenshot $image" }
    $item = Get-Item $image
    if ($item.Length -lt 20000) { throw "The $group fullscreen screenshot seems blank: $($item.Length) bytes" }
    "PASS qml-fullscreen-$group.png ($($item.Length) bytes)"
}
