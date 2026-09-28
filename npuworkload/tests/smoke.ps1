param(
    [string]$Executable = "",
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not $Executable) { $Executable = Join-Path $ProjectRoot "build\desktop-release\npu-avs-workload.exe" }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $ProjectRoot "build\smoke-results" }
if (-not (Test-Path -LiteralPath $Executable)) { throw "Benchmark executable not found: $Executable" }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

function Invoke-SmokeCase {
    param([string]$Name, [string[]]$Arguments, [int]$ExpectedExit, [string]$ExpectedResult)
    $OutputPath = Join-Path $OutputDirectory ($Name + ".jsonl")
    & $Executable @Arguments --output $OutputPath
    $ActualExit = $LASTEXITCODE
    if ($ActualExit -ne $ExpectedExit) { throw "$Name returned exit $ActualExit; expected $ExpectedExit" }
    $Records = @(Get-Content -LiteralPath $OutputPath | ForEach-Object { $_ | ConvertFrom-Json })
    if ($Records.Count -eq 0 -or $Records[-1].type -ne "summary") { throw "$Name has no final summary" }
    if ($Records[-1].result -ne $ExpectedResult) {
        throw "$Name returned result $($Records[-1].result); expected $ExpectedResult"
    }
    [pscustomobject]@{
        Test = $Name; ExitCode = $ActualExit; Result = $Records[-1].result
        Inferences = $Records[-1].inference_count; Verified = $Records[-1].verify_pass
        Target = $Records[-1].execution_target
    }
}

Invoke-SmokeCase "null" @(
    "--profile", "null", "--duration", "0", "--inferences", "3", "--warmup-inferences", "0", "--summary-only"
) 0 "PASS"

Invoke-SmokeCase "reference-repeatability" @(
    "--profile", "reference", "--duration", "0", "--inferences", "5", "--warmup-inferences", "1", "--summary-only"
) 0 "PASS"

Invoke-SmokeCase "checksum-failure" @(
    "--profile", "reference", "--duration", "0", "--inferences", "1", "--warmup-inferences", "0",
    "--golden-checksum", "0000000000000000", "--summary-only"
) 1 "CHECKSUM_FAIL"

Invoke-SmokeCase "inference-timeout" @(
    "--profile", "null", "--duration", "0", "--inferences", "1", "--warmup-inferences", "0",
    "--simulated-latency-ms", "30", "--inference-timeout-ms", "1", "--summary-only"
) 3 "TIMEOUT"

Invoke-SmokeCase "unsupported-backend" @(
    "--profile", "reference", "--backend", "missing", "--duration", "0", "--inferences", "1", "--summary-only"
) 2 "API_ERROR"

$LifecyclePath = Join-Path $OutputDirectory "lifecycle.jsonl"
& $Executable --profile reference --duration 0 --inferences 3 --warmup-inferences 1 `
    --heartbeat-interval 0.0001 --per-inference-log --output $LifecyclePath
if ($LASTEXITCODE -ne 0) { throw "lifecycle test failed" }
$Lifecycle = @(Get-Content -LiteralPath $LifecyclePath | ForEach-Object { $_ | ConvertFrom-Json })
$Types = @($Lifecycle | ForEach-Object { $_.type })
foreach ($Required in @("start", "heartbeat", "inference", "verify", "summary")) {
    if ($Required -notin $Types) { throw "lifecycle output is missing $Required" }
}
if ($Lifecycle[-1].execution_target -ne "cpu_reference" -or $Lifecycle[-1].fallback_used) {
    throw "reference backend execution metadata is incorrect"
}
