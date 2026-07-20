# Full quick validation pass (unit + stress/fuzz/fault/recovery/soak).
param(
  [string]$BuildDir = "build",
  [switch]$FaultInject,
  [switch]$Configure
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $root

$vs = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
if (-not (Test-Path $vs)) {
  $vs = "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"
}

$fi = if ($FaultInject) { "ON" } else { "OFF" }

if ($Configure -or -not (Test-Path "$BuildDir\CMakeCache.txt")) {
  $cfg = "cmake -S core -B $BuildDir -DEMQ_BUILD_STRESS=ON -DEMQ_FAULT_INJECT=$fi -DEMQ_BUILD_BENCH=ON"
  cmd /c "`"$vs`" -arch=x64 -no_logo && $cfg"
  if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
}

cmd /c "`"$vs`" -arch=x64 -no_logo && cmake --build $BuildDir --config Release"
if ($LASTEXITCODE -ne 0) { throw "build failed" }

ctest --test-dir $BuildDir -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "ctest failed" }

Write-Host "run_all.ps1 OK" -ForegroundColor Green
