param(
    [string]$Executable = "",
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not $Executable) {
    $Executable = Join-Path $ProjectRoot "build\desktop-release\cpu-avs-workload.exe"
}
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $ProjectRoot "build\smoke-results"
}
if (-not (Test-Path -LiteralPath $Executable)) {
    throw "Benchmark executable not found: $Executable"
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

function Invoke-SmokeCase {
    param(
        [string]$Name,
        [string[]]$Arguments,
        [int]$ExpectedExit,
        [string]$ExpectedResult
    )

    $OutputPath = Join-Path $OutputDirectory ($Name + ".jsonl")
    & $Executable @Arguments --output $OutputPath
    $ActualExit = $LASTEXITCODE
    if ($ActualExit -ne $ExpectedExit) {
        throw "$Name returned exit $ActualExit; expected $ExpectedExit"
    }

    $Records = @(Get-Content -LiteralPath $OutputPath | ForEach-Object { $_ | ConvertFrom-Json })
    if ($Records.Count -eq 0 -or $Records[-1].type -ne "summary") {
        throw "$Name did not produce a final summary record"
    }
    if ($Records[-1].result -ne $ExpectedResult) {
        throw "$Name returned result $($Records[-1].result); expected $ExpectedResult"
    }

    [pscustomobject]@{
        Test = $Name
        ExitCode = $ActualExit
        Result = $Records[-1].result
        Batches = $Records[-1].batch_count
        Verified = $Records[-1].verify_pass
    }
}

Invoke-SmokeCase "integer" @(
    "--profile", "integer", "--duration", "0", "--batches", "3", "--warmup", "0", "--summary-only"
) 0 "PASS"

Invoke-SmokeCase "floating-point" @(
    "--profile", "floating_point", "--duration", "0", "--batches", "3", "--warmup", "0", "--summary-only"
) 0 "PASS"

Invoke-SmokeCase "matrix" @(
    "--profile", "matrix", "--duration", "0", "--batches", "2", "--warmup", "0", "--summary-only"
) 0 "PASS"

Invoke-SmokeCase "memory" @(
    "--profile", "memory", "--duration", "0", "--batches", "2", "--warmup", "0", "--summary-only"
) 0 "PASS"

Invoke-SmokeCase "mixed-multithread" @(
    "--profile", "mixed", "--duration", "0", "--batches", "3", "--warmup", "0", "--threads", "2", "--summary-only"
) 0 "PASS"

Invoke-SmokeCase "checksum-failure" @(
    "--profile", "integer", "--duration", "0", "--batches", "1", "--warmup", "0",
    "--golden-checksum", "0000000000000000", "--summary-only"
) 1 "CHECKSUM_FAIL"

Invoke-SmokeCase "performance-failure" @(
    "--profile", "integer", "--duration", "0", "--batches", "2", "--warmup", "0",
    "--min-operations-per-sec", "9999999999999", "--fail-on-instability", "--summary-only"
) 7 "PERFORMANCE_FAIL"

$VerifyCadencePath = Join-Path $OutputDirectory "verify-cadence.jsonl"
& $Executable --profile integer --duration 0 --batches 5 --warmup 0 `
    --verify-interval 1 --success-log-interval 2 --output $VerifyCadencePath
if ($LASTEXITCODE -ne 0) {
    throw "verify-cadence returned exit $LASTEXITCODE; expected 0"
}
$CadenceRecords = @(Get-Content -LiteralPath $VerifyCadencePath |
    ForEach-Object { $_ | ConvertFrom-Json })
$VerifyRecords = @($CadenceRecords | Where-Object { $_.type -eq "verify" })
$VerifyBatches = @($VerifyRecords | ForEach-Object { [int]$_.batch })
if ($VerifyBatches.Count -lt 1 -or $VerifyBatches[0] -ne 1 -or @($VerifyBatches | Where-Object { $_ -notin @(1,3,5) }).Count) {
    throw "verify-cadence emitted invalid sampled batches [$($VerifyBatches -join ',')]; rate cap may omit candidates after batch 1"
}
if ([int]$CadenceRecords[-1].verify_count -ne 5) {
    throw "verify-cadence summary reported verify_count=$($CadenceRecords[-1].verify_count); expected 5"
}

$VerifyIntervalPath = Join-Path $OutputDirectory "verify-interval.jsonl"
& $Executable --profile integer --duration 0 --batches 5 --warmup 0 `
    --verify-interval 2 --success-log-interval 1 --output $VerifyIntervalPath
if ($LASTEXITCODE -ne 0) {
    throw "verify-interval returned exit $LASTEXITCODE; expected 0"
}
$AllIntervalRecords = @(Get-Content -LiteralPath $VerifyIntervalPath |
    ForEach-Object { $_ | ConvertFrom-Json })
$IntervalRecords = @($AllIntervalRecords | Where-Object { $_.type -eq "verify" })
$IntervalBatches = @($IntervalRecords | ForEach-Object { [int]$_.batch })
if ($IntervalBatches.Count -lt 1 -or $IntervalBatches[0] -ne 2 -or @($IntervalBatches | Where-Object { $_ -notin @(2,4) }).Count) {
    throw "verify-interval emitted invalid sampled batches [$($IntervalBatches -join ',')]; first check must be batch 2"
}
if ([int]$IntervalRecords.Count -gt 2) {
    throw "verify-interval emitted too many verify events: $($IntervalRecords.Count)"
}
if ([int]$AllIntervalRecords[-1].verify_count -ne 2) {
    throw "verify-interval summary reported verify_count=$($AllIntervalRecords[-1].verify_count); expected 2"
}

$FailurePath = Join-Path $OutputDirectory "verify-failure-unsuppressed.jsonl"
& $Executable --profile integer --duration 0 --batches 1 --warmup 0 `
    --golden-checksum 0000000000000000 --success-log-interval 0 --output $FailurePath
if ($LASTEXITCODE -ne 1) {
    throw "verify-failure-unsuppressed returned exit $LASTEXITCODE; expected 1"
}
$FailureVerify = @(Get-Content -LiteralPath $FailurePath |
    ForEach-Object { $_ | ConvertFrom-Json } |
    Where-Object { $_.type -eq "verify" })
if ($FailureVerify.Count -ne 1 -or $FailureVerify[0].result -ne "FAIL") {
    throw "failed verification was suppressed by success-log-interval=0"
}

[pscustomobject]@{ Test = "verify-cadence"; ExitCode = 0; Result = "PASS"; Batches = 5; Verified = $true }
[pscustomobject]@{ Test = "verify-interval"; ExitCode = 0; Result = "PASS"; Batches = 5; Verified = $true }
[pscustomobject]@{ Test = "verify-failure-unsuppressed"; ExitCode = 1; Result = "CHECKSUM_FAIL"; Batches = 1; Verified = $false }
