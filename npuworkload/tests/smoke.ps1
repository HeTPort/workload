param(
    [string]$Executable = "",
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$FrameworkManifest = Join-Path $ProjectRoot "profiles\framework_smoke\manifest.json"
if (-not $Executable) { $Executable = Join-Path $ProjectRoot "build\desktop-release\npu-avs-workload.exe" }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $ProjectRoot "build\smoke-results" }
if (-not (Test-Path -LiteralPath $Executable)) { throw "Benchmark executable not found: $Executable" }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

$ContractExecutable = Join-Path (Split-Path -Parent $Executable) "npu-contract-tests.exe"
if (-not (Test-Path -LiteralPath $ContractExecutable)) {
    throw "Contract test executable not found: $ContractExecutable"
}
& $ContractExecutable $FrameworkManifest
if ($LASTEXITCODE -ne 0) { throw "contract tests failed with exit $LASTEXITCODE" }

$Profiles = @(& $Executable --list-profiles)
foreach ($RequiredProfile in @("kws01", "ic01", "ad01", "sww01", "framework_smoke")) {
    if ($RequiredProfile -notin $Profiles) { throw "profile list is missing $RequiredProfile" }
}

function Invoke-SmokeCase {
    param([string]$Name, [string[]]$Arguments, [int]$ExpectedExit, [string]$ExpectedResult)
    $OutputPath = Join-Path $OutputDirectory ($Name + ".jsonl")
    $EffectiveArguments = $Arguments
    if ($Arguments -contains "framework_smoke") {
        $EffectiveArguments += @("--input-manifest", $FrameworkManifest)
    }
    & $Executable @EffectiveArguments --output $OutputPath
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
    "--profile", "framework_smoke", "--backend", "null", "--duration", "0", "--inferences", "3",
    "--warmup-inferences", "0", "--summary-only"
) 0 "PASS"

Invoke-SmokeCase "reference-repeatability" @(
    "--profile", "framework_smoke", "--backend", "reference_cpu", "--duration", "0",
    "--inferences", "5", "--warmup-inferences", "1", "--summary-only"
) 0 "PASS"

Invoke-SmokeCase "checksum-failure" @(
    "--profile", "framework_smoke", "--backend", "reference_cpu", "--duration", "0",
    "--inferences", "1", "--warmup-inferences", "0",
    "--golden-checksum", "0000000000000000", "--summary-only"
) 1 "CHECKSUM_FAIL"

$TimeoutWatch = [System.Diagnostics.Stopwatch]::StartNew()
Invoke-SmokeCase "inference-timeout" @(
    "--profile", "framework_smoke", "--backend", "null", "--duration", "0", "--inferences", "1",
    "--warmup-inferences", "0", "--simulated-latency-ms", "3000", "--inference-timeout-ms", "1",
    "--summary-only"
) 3 "TIMEOUT"
$TimeoutWatch.Stop()
if ($TimeoutWatch.ElapsedMilliseconds -gt 1000) {
    throw "timeout cleanup took $($TimeoutWatch.ElapsedMilliseconds) ms; expected at most 1000 ms"
}

$GlobalTimeoutWatch = [System.Diagnostics.Stopwatch]::StartNew()
Invoke-SmokeCase "global-timeout-bound" @(
    "--profile", "framework_smoke", "--backend", "null", "--duration", "0", "--inferences", "1",
    "--warmup-inferences", "0", "--simulated-latency-ms", "3000", "--inference-timeout-ms", "5000",
    "--timeout", "0.01", "--summary-only"
) 3 "TIMEOUT"
$GlobalTimeoutWatch.Stop()
if ($GlobalTimeoutWatch.ElapsedMilliseconds -gt 1000) {
    throw "global deadline cleanup took $($GlobalTimeoutWatch.ElapsedMilliseconds) ms; expected at most 1000 ms"
}

Invoke-SmokeCase "unsupported-backend" @(
    "--profile", "framework_smoke", "--backend", "missing", "--duration", "0", "--inferences", "1", "--summary-only"
) 2 "API_ERROR"

Invoke-SmokeCase "unsupported-profile" @(
    "--profile", "kws01", "--backend", "reference_cpu", "--duration", "0", "--inferences", "1", "--summary-only"
) 2 "API_ERROR"

$LifecyclePath = Join-Path $OutputDirectory "lifecycle.jsonl"
& $Executable --profile framework_smoke --backend reference_cpu --input-manifest $FrameworkManifest `
    --duration 0 --inferences 3 --warmup-inferences 1 `
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
$InferenceRecords = @($Lifecycle | Where-Object { $_.type -eq "inference" })
if ($InferenceRecords.Count -eq 0 -or ($InferenceRecords | Where-Object { $_.output_count -ne 2 }).Count -ne 0) {
    throw "multi-output inference contract was not reported consistently"
}
