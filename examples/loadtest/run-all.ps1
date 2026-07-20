# Run comparable queue load tests for all published clients (Docker).
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$N = if ($env:EMQ_LOAD_N) { $env:EMQ_LOAD_N } else { "100000" }
$Payload = if ($env:EMQ_LOAD_PAYLOAD) { $env:EMQ_LOAD_PAYLOAD } else { "64" }
$Out = Join-Path $PSScriptRoot "results.txt"
"" | Set-Content -Path $Out

Write-Host "EmbeddedMQ cross-client load test  N=$N payload=$Payload"
Write-Host "========================================================"

function Invoke-LoadLang {
    param(
        [string]$Lang,
        [string[]]$DockerArgs
    )
    Write-Host ""
    Write-Host ">>> $Lang"
    $log = Join-Path $env:TEMP "emq-load-$Lang.log"
    & docker @DockerArgs 2>&1 | Tee-Object -FilePath $log | Tee-Object -FilePath $Out -Append | Out-Host
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAILED: $Lang (exit $LASTEXITCODE)"
        Get-Content $log -Tail 40
        throw "load test failed for $Lang"
    }
    $result = Select-String -Path $log -Pattern '^RESULT ' | Select-Object -Last 1
    if (-not $result) {
        throw "no RESULT line for $Lang"
    }
}

$pyDir = Join-Path $Root "examples\loadtest\python"
Invoke-LoadLang python @(
    "run", "--rm",
    "-e", "EMQ_LOAD_N=$N", "-e", "EMQ_LOAD_PAYLOAD=$Payload",
    "-v", "${pyDir}:/app", "-w", "/app",
    "python:3.12-bookworm",
    "bash", "-c",
    @'
set -e
apt-get update -qq
DEBIAN_FRONTEND=noninteractive apt-get install -y -qq build-essential >/tmp/a.log
pip -q install -U pip
pip -q install "embeddedmq==1.0.0b1"
python loadtest.py
'@
)

$rsDir = Join-Path $Root "examples\loadtest\rust"
Invoke-LoadLang rust @(
    "run", "--rm",
    "-e", "EMQ_LOAD_N=$N", "-e", "EMQ_LOAD_PAYLOAD=$Payload",
    "-v", "${rsDir}:/app", "-w", "/app",
    "rust:1.85-bookworm",
    "bash", "-c",
    @'
export PATH=/usr/local/cargo/bin:$PATH CARGO_TARGET_DIR=/tmp/ct
set -e
cargo build --release -q
cargo run --release -q
'@
)

# Go nested module needs sibling bindings/native (replace in go.mod).
Invoke-LoadLang go @(
    "run", "--rm",
    "-e", "EMQ_LOAD_N=$N", "-e", "EMQ_LOAD_PAYLOAD=$Payload",
    "-v", "${Root}:/src", "-w", "/src/examples/loadtest/go",
    "golang:1.22-bookworm",
    "bash", "-c",
    @'
set -e
apt-get update -qq
DEBIAN_FRONTEND=noninteractive apt-get install -y -qq gcc >/tmp/a.log
export CGO_ENABLED=1
go mod tidy
go run .
'@
)

$jvDir = Join-Path $Root "examples\loadtest\java"
Invoke-LoadLang java @(
    "run", "--rm",
    "-e", "EMQ_LOAD_N=$N", "-e", "EMQ_LOAD_PAYLOAD=$Payload",
    "-v", "${jvDir}:/app", "-w", "/app",
    "maven:3.9-eclipse-temurin-22",
    "bash", "-c",
    @'
set -e
mvn -q -B package
java --enable-native-access=ALL-UNNAMED -jar target/java-loadtest-1.0.0-SNAPSHOT.jar
'@
)

Write-Host ""
Write-Host "========================================================"
Write-Host "Summary (RESULT lines):"
Select-String -Path $Out -Pattern '^RESULT ' | ForEach-Object { $_.Line }
Write-Host "Full log: $Out"
