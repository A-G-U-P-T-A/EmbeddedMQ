# Long stress runs (not part of default ctest).
param(
  [string]$BuildDir = "build",
  [ValidateSet("spsc", "mpmc", "churn", "all")]
  [string]$Suite = "all",
  [uint64]$Ops = 100000000
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$bin = Join-Path $root "$BuildDir\stress"

function Invoke-Stress {
  param([string]$Name, [string[]]$ArgList)
  $exe = Join-Path $bin "$Name.exe"
  if (-not (Test-Path $exe)) { $exe = Join-Path $bin $Name }
  Write-Host ">>> $Name $($ArgList -join ' ')" -ForegroundColor Cyan
  & $exe @ArgList
  if ($LASTEXITCODE -ne 0) { throw "$Name failed" }
}

if ($Suite -eq "spsc" -or $Suite -eq "all") {
  Invoke-Stress "stress_spsc" @("--ops", "$Ops", "--payload", "64")
}
if ($Suite -eq "mpmc" -or $Suite -eq "all") {
  $opsM = [Math]::Min([double]$Ops, 50000000)
  Invoke-Stress "stress_mpmc" @("--ops", "$opsM", "--queues", "100", "--threads", "16")
}
if ($Suite -eq "churn" -or $Suite -eq "all") {
  Invoke-Stress "stress_churn" @("--duration", "300", "--queues", "1000")
}

Write-Host "stress_long.ps1 OK" -ForegroundColor Green
