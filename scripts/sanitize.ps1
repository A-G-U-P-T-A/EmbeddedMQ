# Run sanitizer builds under WSL (Clang).
# Usage:
#   .\scripts\sanitize.ps1                 # ASan+UBSan then unit+quick stress
#   .\scripts\sanitize.ps1 -Mode thread    # ThreadSanitizer
#   .\scripts\sanitize.ps1 -Mode valgrind  # Valgrind memcheck on unit tests

param(
  [ValidateSet("address", "thread", "valgrind")]
  [string]$Mode = "address"
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")

function Invoke-Wsl {
  param([string]$Cmd)
  Write-Host "WSL> $Cmd" -ForegroundColor Cyan
  wsl -e bash -lc "cd '$($root -replace '\\','/')' && $Cmd"
  if ($LASTEXITCODE -ne 0) { throw "WSL command failed: $Cmd" }
}

if ($Mode -eq "valgrind") {
  Invoke-Wsl @"
set -e
cmake -S core -B build-valgrind -DCMAKE_BUILD_TYPE=Debug -DEMQ_BUILD_STRESS=OFF -DEMQ_BUILD_BENCH=OFF -DEMQ_BUILD_EXAMPLES=OFF
cmake --build build-valgrind -j
cd build-valgrind
ctest --output-on-failure
for t in tests/test_*; do
  if [ -x `"`$t`" ]; then
    echo VALGRIND `$t
    valgrind --error-exitcode=1 --leak-check=full --show-leak-kinds=definite `$t
  fi
done
"@
  exit 0
}

$sans = if ($Mode -eq "thread") { "thread" } else { "address;undefined" }
$build = "build-san-$Mode"

Invoke-Wsl @"
set -e
cmake -S core -B $build -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=clang \
  -DEMQ_SANITIZE='$sans' \
  -DEMQ_BUILD_STRESS=ON -DEMQ_FAULT_INJECT=ON \
  -DEMQ_BUILD_BENCH=OFF -DEMQ_BUILD_EXAMPLES=OFF
cmake --build $build -j
cd $build
export TSAN_OPTIONS="halt_on_error=1:suppressions=$($root -replace '\\','/')/core/cmake/tsan_suppressions.txt"
ctest --output-on-failure -L 'stress|fuzz|fault|recovery|soak|difftest|model' --timeout 180 || true
ctest --output-on-failure -E 'stress_|fuzz_|fault_|recovery_|soak_' --timeout 60
"@

Write-Host "sanitize.ps1 ($Mode) OK" -ForegroundColor Green
