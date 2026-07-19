# Performance regression gate.
# Runs emq_bench_load --quick three times, takes median ops/s and p99 per cell,
# fails if throughput < -10% or p99 > +10% vs baseline CSV.
#
#   .\scripts\perf_check.ps1
#   .\scripts\perf_check.ps1 -Update   # refresh baseline

param(
  [string]$BuildDir = "build",
  [string]$Baseline = "bench/baselines/windows-x64.csv",
  [switch]$Update
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $root

$exe = Join-Path $root "$BuildDir\bench\emq_bench_load.exe"
if (-not (Test-Path $exe)) { $exe = Join-Path $root "$BuildDir\bench\emq_bench_load" }
if (-not (Test-Path $exe)) { throw "emq_bench_load not found; build first" }

New-Item -ItemType Directory -Force -Path (Split-Path $Baseline) | Out-Null
$tmpDir = Join-Path $env:TEMP "emq_perf_$PID"
New-Item -ItemType Directory -Force -Path $tmpDir | Out-Null

$runs = @()
for ($i = 1; $i -le 3; $i++) {
  $csv = Join-Path $tmpDir "run$i.csv"
  & $exe --quick --csv $csv | Out-Host
  if ($LASTEXITCODE -ne 0) { throw "bench_load failed" }
  $runs += $csv
}

function Read-Cells($path) {
  $rows = Import-Csv $path
  return $rows
}

# Median of 3 numeric values
function Median3([double]$a, [double]$b, [double]$c) {
  $arr = @($a, $b, $c) | Sort-Object
  return $arr[1]
}

$c0 = Read-Cells $runs[0]
$c1 = Read-Cells $runs[1]
$c2 = Read-Cells $runs[2]
if ($c0.Count -ne $c1.Count -or $c0.Count -ne $c2.Count) {
  throw "run cell counts differ"
}

$medianPath = Join-Path $tmpDir "median.csv"
# Reconstruct a median CSV matching bench_metrics header if present
$header = Get-Content $runs[0] -TotalCount 1
$outLines = @($header)
for ($i = 0; $i -lt $c0.Count; $i++) {
  $ops = Median3 ([double]$c0[$i].ops_per_sec) ([double]$c1[$i].ops_per_sec) ([double]$c2[$i].ops_per_sec)
  $p99 = Median3 ([double]$c0[$i].p99_ns) ([double]$c1[$i].p99_ns) ([double]$c2[$i].p99_ns)
  # Copy row from run0 and patch median fields (column names from bench_metrics)
  $row = $c0[$i].PSObject.Copy()
  if ($row.PSObject.Properties.Name -contains "ops_per_sec") { $row.ops_per_sec = $ops }
  if ($row.PSObject.Properties.Name -contains "p99_ns") { $row.p99_ns = [uint64]$p99 }
  $vals = foreach ($p in $row.PSObject.Properties) { $p.Value }
  $outLines += ($vals -join ",")
}
$outLines | Set-Content -Path $medianPath -Encoding ascii

if ($Update -or -not (Test-Path $Baseline)) {
  Copy-Item $medianPath $Baseline -Force
  Write-Host "Baseline written: $Baseline" -ForegroundColor Green
  Remove-Item -Recurse -Force $tmpDir
  exit 0
}

# Compare against baseline
$base = Read-Cells $Baseline
$med = Read-Cells $medianPath
$failed = 0
$n = [Math]::Min($base.Count, $med.Count)
for ($i = 0; $i -lt $n; $i++) {
  $bOps = [double]$base[$i].ops_per_sec
  $mOps = [double]$med[$i].ops_per_sec
  $bP99 = [double]$base[$i].p99_ns
  $mP99 = [double]$med[$i].p99_ns
  if ($bOps -le 0) { continue }
  $thrDelta = ($mOps - $bOps) / $bOps
  $p99Delta = if ($bP99 -gt 0) { ($mP99 - $bP99) / $bP99 } else { 0 }
  $label = "queues=$($med[$i].queues) payload=$($med[$i].payload)"
  if ($thrDelta -lt -0.10) {
    Write-Host "REGRESS throughput $label : $([Math]::Round($thrDelta*100,1))%" -ForegroundColor Red
    $failed = 1
  }
  if ($p99Delta -gt 0.10) {
    Write-Host "REGRESS p99 $label : +$([Math]::Round($p99Delta*100,1))%" -ForegroundColor Red
    $failed = 1
  }
  Write-Host ("OK {0}: thr {1:P1} p99 {2:P1}" -f $label, $thrDelta, $p99Delta)
}

Remove-Item -Recurse -Force $tmpDir
if ($failed) { throw "perf_check FAILED" }
Write-Host "perf_check OK" -ForegroundColor Green
