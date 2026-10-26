param([string]$Compiler = "", [string]$MonitorWorkloadDirectory = "")

$ErrorActionPreference = "Stop"
$RepositoryRoot = Split-Path -Parent $PSScriptRoot
if (-not $Compiler) { $Compiler = (Get-Command g++ -ErrorAction Stop).Source }
$TemporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$TestDirectory = Join-Path $TemporaryRoot ("avs-runtime-contract-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $TestDirectory | Out-Null

function Assert-Contract([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Invoke-Workload([string]$Executable, [string[]]$InvocationArguments, [int]$ExpectedExit = 0) {
    $Lines = @(& $Executable @InvocationArguments)
    Assert-Contract ($LASTEXITCODE -eq $ExpectedExit) "Unexpected exit $LASTEXITCODE for $InvocationArguments"
    return @($Lines | ForEach-Object { $_ | ConvertFrom-Json })
}

try {
    foreach ($Kind in @("cpu", "gpu")) {
        $Project = Join-Path $RepositoryRoot ($Kind + "workload")
        $Executable = Join-Path $TestDirectory ($Kind + "-avs-workload.exe")
        $Flags = @("-std=c++17", "-O1", "-Wall", "-Wextra", "-Wpedantic", "-pthread",
                   "-I", (Join-Path $Project "include"), "-I", (Join-Path $Project "src"))
        if ($Kind -eq "cpu") {
            $Sources = @(Get-ChildItem -LiteralPath (Join-Path $Project "src") -Filter "*.cpp" -Recurse | ForEach-Object FullName)
            $RunArguments = @("--profile", "integer", "--backend", "integer", "--iterations", "1000",
                              "--threads", "1", "--batches", "100")
            $CountField = "batch_count"
        } else {
            $Sources = @(Get-ChildItem -LiteralPath (Join-Path $Project "src") -Filter "*.cpp" | ForEach-Object FullName)
            $Sources += Join-Path $Project "src\backends\null\null_backend.cpp"
            $Flags += @("-DGPU_AVS_ENABLE_NULL=1", "-DGPU_AVS_ENABLE_GLES=0",
                        "-DGPU_AVS_ENABLE_VULKAN=0", "-DGPU_AVS_ENABLE_OPENCL=0")
            $RunArguments = @("--profile", "mixed", "--api", "null", "--width", "8", "--height", "8", "--frames", "100")
            $CountField = "frame_count"
        }
        & $Compiler @Flags @Sources -o $Executable
        Assert-Contract ($LASTEXITCODE -eq 0) "$Kind runtime build failed"
        $Capabilities = Invoke-Workload $Executable @("--capabilities")
        Assert-Contract ($Capabilities.contract_version -eq 2 -and $Capabilities.version -eq "2.1.0") "$Kind missing capabilities"
        Assert-Contract ($Capabilities.features -contains "failure_verify") "$Kind missing failure_verify feature"
        if ($MonitorWorkloadDirectory) {
            foreach ($ConfigFile in Get-ChildItem -LiteralPath $MonitorWorkloadDirectory -Filter ($Kind + "*.json")) {
                $Effective = Invoke-Workload $Executable @("--config", $ConfigFile.FullName, "--dump-effective-config")
                $Requested = Get-Content -LiteralPath $ConfigFile.FullName -Raw | ConvertFrom-Json
                Assert-Contract ($Effective.verify_mode -eq $Requested.verify_mode -and
                    $Effective.verify_interval -eq $Requested.verify_interval -and
                    $Effective.success_log_interval -eq $Requested.success_log_interval) "Effective config mismatch: $($ConfigFile.Name)"
            }
        }
        if ($Kind -eq "cpu") {
            & (Join-Path $RepositoryRoot "cpuworkload/tests/smoke.ps1") -Executable $Executable -OutputDirectory (Join-Path $TestDirectory "cpu-smoke")
        }
        $RunArguments += @("--duration", "0", "--warmup", "0", "--timeout", "10",
                           "--verify-mode", "checksum", "--verify-interval", "1", "--heartbeat-interval", "0.001")

        # Reference checksum is captured from a real runner pass; GPU checksum
        # without this reference is recording only and is not a correctness test.
        $Capture = @(Invoke-Workload $Executable ($RunArguments + @("--success-log-interval", "0")))
        $Reference = ($Capture | Where-Object type -eq "summary").checksum
        $VerifiedArguments = $RunArguments + @("--golden-checksum", $Reference)
        $Events = @(Invoke-Workload $Executable ($VerifiedArguments + @("--summary-only", "--success-log-interval", "0")))
        $Summary = $Events | Where-Object type -eq "summary"
        Assert-Contract ($Summary.verify_count -eq 100 -and $Summary.$CountField -eq 100) "$Kind incomplete verification"
        Assert-Contract ($Summary.verify_fail_count -eq 0 -and $Summary.verify_pass) "$Kind correct reference failed"
        Assert-Contract (@($Events | Where-Object type -eq "verify").Count -eq 0) "$Kind success suppression failed"
        Assert-Contract (@($Events | Where-Object type -eq "heartbeat").Count -gt 0) "$Kind summary_only suppressed heartbeat"
        Assert-Contract ($Summary.contract_version -eq 2 -and $Summary.verify_interval -eq 1) "$Kind missing summary contract"

        $Sampled = @(Invoke-Workload $Executable ($VerifiedArguments + @("--success-log-interval", "1")))
        $Successes = @($Sampled | Where-Object type -eq "verify")
        $SampleSummary = $Sampled | Where-Object type -eq "summary"
        Assert-Contract ($Successes.Count -gt 0 -and $Successes[0].pass -eq $true) "$Kind missing first success/pass boolean"
        Assert-Contract ($Successes.Count -le (1 + [math]::Floor($SampleSummary.duration_s))) "$Kind success rate cap exceeded"
        Assert-Contract ($SampleSummary.verify_count -eq 100) "$Kind logging cap changed coverage"

        $Failure = @(Invoke-Workload $Executable ($RunArguments + @("--golden-checksum", "0x0",
                         "--summary-only", "--success-log-interval", "0")) 1)
        $FailureSummary = $Failure | Where-Object type -eq "summary"
        $FailureEvents = @($Failure | Where-Object type -eq "verify")
        Assert-Contract ($FailureEvents.Count -eq 1 -and $FailureEvents[0].pass -eq $false) "$Kind failure hidden"
        Assert-Contract ($FailureEvents[0].result -eq "FAIL" -and $Failure[-1].type -eq "summary") "$Kind failure must precede summary"
        Assert-Contract ($FailureSummary.verify_count -eq 1 -and $FailureSummary.verify_fail_count -eq 1) "$Kind fail-fast count mismatch"

        $AllFailures = @(Invoke-Workload $Executable ($RunArguments + @("--golden-checksum", "0x0",
                        "--summary-only", "--success-log-interval", "0", "--fail-fast", "false")) 1)
        Assert-Contract (@($AllFailures | Where-Object type -eq "verify").Count -eq 100) "$Kind failures were rate limited"

        if ($Kind -eq "gpu") {
            $GoldenPath = Join-Path $TestDirectory "gpu-golden.rgba"
            $GoldenArgs = @("--profile", "mixed", "--api", "null", "--width", "8", "--height", "8",
                            "--warmup", "0", "--golden-file", $GoldenPath, "--verify-mode", "golden-image")
            $Golden = @(Invoke-Workload $Executable ($GoldenArgs + @("--generate-golden")))
            Assert-Contract ((Get-Item -LiteralPath $GoldenPath).Length -eq 256) "GPU golden size mismatch"
            Assert-Contract (@($Golden | Where-Object type -eq "golden").Count -eq 1) "GPU missing golden event"
            $Exact = @(Invoke-Workload $Executable ($GoldenArgs + @("--duration", "0", "--frames", "5",
                              "--verify-interval", "1", "--success-log-interval", "0")))
            $ExactSummary = $Exact | Where-Object type -eq "summary"
            Assert-Contract ($ExactSummary.verify_count -eq 5 -and $ExactSummary.verify_pass) "GPU exact golden runner failed"
        }
        [pscustomobject]@{ Test = "$Kind-runtime-contract"; Result = "PASS" }
    }
} finally {
    $ResolvedTestDirectory = [IO.Path]::GetFullPath($TestDirectory)
    if (-not $ResolvedTestDirectory.StartsWith($TemporaryRoot, [StringComparison]::OrdinalIgnoreCase) -or
        $ResolvedTestDirectory -eq $TemporaryRoot) { throw "Unexpected test directory: $ResolvedTestDirectory" }
    Remove-Item -LiteralPath $ResolvedTestDirectory -Recurse -Force
}
