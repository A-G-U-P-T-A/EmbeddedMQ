# Soak test runner.
#   .\scripts\soak.ps1              # quick 30s
#   .\scripts\soak.ps1 -Hours 24    # long soak
param(
  [string]$BuildDir = "build",
  [double]$Hours = 0,
  [int]$Seconds = 30,
  [string]$Csv = "soak.csv"
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$exe = Join-Path $root "$BuildDir\soak\soak_runner.exe"
if (-not (Test-Path $exe)) {
  $exe = Join-Path $root "$BuildDir\soak\soak_runner"
}

$dur = if ($Hours -gt 0) { [uint64]($Hours * 3600) } else { [uint64]$Seconds }
Write-Host "Soak duration=${dur}s csv=$Csv" -ForegroundColor Cyan
& $exe --duration $dur --csv $Csv --queues 8 --threads 4
if ($LASTEXITCODE -ne 0) { throw "soak failed" }
Write-Host "soak.ps1 OK" -ForegroundColor Green
