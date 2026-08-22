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
