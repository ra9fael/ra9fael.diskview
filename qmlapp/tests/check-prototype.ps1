param([string]$Root = "$PSScriptRoot\..")
$ErrorActionPreference = 'Stop'
$build = Join-Path $Root 'build'
$run = Join-Path $build 'run'
node (Join-Path $Root 'tests\check-fixture.cjs')
if ($LASTEXITCODE -ne 0) { throw 'Fixture totals are inconsistent' }
$exe = Join-Path $run 'diskview-qml-prototype.exe'
$sdk = 'D:\tools\Qt\6.12.0\msvc2022_64'
$geometryExe = Join-Path $run 'diskview-chart-geometry-tests.exe'
if (-not (Test-Path $geometryExe)) {
  $vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
  $compiler = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64\cl.exe'
  & cmd /c "call `"$vcvars`" >nul && `"$compiler`" /nologo /utf-8 /std:c++20 /EHsc /MD /I `"$Root\..`" `"$Root\tests\chart-geometry-tests.cpp`" /Fe:`"$geometryExe`""
  if ($LASTEXITCODE -ne 0) { throw 'Could not compile chart geometry tests' }
}
& $geometryExe
if ($LASTEXITCODE -ne 0) { throw 'Chart geometry unit tests failed' }
if (-not (Test-Path $exe)) { throw "Missing prototype executable: $exe" }
$env:PATH = "$run;$sdk\bin;$env:PATH"
$env:QML2_IMPORT_PATH = "$run\qml"
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_QUICK_BACKEND = 'software'
$env:DISKVIEW_QML_DIR = "$run\qml"
foreach ($mode in 0,1) {
  $name = if ($mode -eq 0) { 'qml-treemap-native.png' } else { 'qml-sunburst-painted.png' }
  $image = Join-Path $build $name
  $env:DISKVIEW_VIEW_MODE = "$mode"
  $env:DISKVIEW_SCREENSHOT = $image
  $hitResult = Join-Path $run "check-$mode.hit"
  Remove-Item $hitResult -ErrorAction SilentlyContinue
  $env:DISKVIEW_HIT_RESULT = $hitResult
  $stderr = Join-Path $run "check-$mode.err"
  & $exe 2> $stderr
  if ($LASTEXITCODE -ne 0) { throw "Prototype failed in view mode $mode, see $stderr" }
  if (-not (Test-Path $image)) { throw "Missing screenshot $image" }
  $item = Get-Item $image
  if ($item.Length -lt 20000) { throw "Screenshot seems blank: $name ($($item.Length) bytes)" }
  $log = Get-Content $stderr -ErrorAction SilentlyContinue
  $errors = $log | Where-Object { $_ -match 'failed to load|TypeError|ReferenceError|QSG.*error|hit-test found no item' }
  if ($errors) { throw "Runtime error in mode ${mode}: $($errors -join '; ')" }
  if (-not (Test-Path $hitResult)) { throw "Missing chart hit-test result in mode $mode" }
  $hit = Get-Content $hitResult
  $identity = $hit[0] -split "`t"
  $checks = $hit[1] -split "`t"
  if ($identity.Count -ne 2 -or $identity[0] -ne "$mode" -or [string]::IsNullOrWhiteSpace($identity[1]) -or $checks.Count -ne 5 -or $checks[0] -ne 'regions' -or $checks[1] -ne 'transform' -or $checks[2] -ne 'navigate' -or $checks[3] -ne 'return' -or $checks[4] -ne 'PASS') { throw "Chart interaction checks failed in mode ${mode}: $($hit -join ' / ')" }
  "PASS $name ($($item.Length) bytes), chart and navigation checks passed: $($hit.Trim())"
}
