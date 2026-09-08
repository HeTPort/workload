param(
    [string]$Compiler = ""
)

$ErrorActionPreference = "Stop"
$RepositoryRoot = Split-Path -Parent $PSScriptRoot
if (-not $Compiler) {
    $CompilerCommand = Get-Command g++ -ErrorAction Stop
    $Compiler = $CompilerCommand.Source
}

$CpuHeader = Join-Path $RepositoryRoot "cpuworkload\include\avs\verify_schedule.h"
$GpuHeader = Join-Path $RepositoryRoot "gpuworkload\include\avs\verify_schedule.h"
if ((Get-FileHash -Algorithm SHA256 $CpuHeader).Hash -ne
    (Get-FileHash -Algorithm SHA256 $GpuHeader).Hash) {
    throw "CPU and GPU verify_schedule.h implementations differ"
}

$TemporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$TestDirectory = Join-Path $TemporaryRoot ("avs-verify-contract-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $TestDirectory | Out-Null

function Invoke-CompileAndRun {
    param(
        [string]$Name,
        [string[]]$Arguments
    )

    $Executable = Join-Path $TestDirectory ($Name + ".exe")
    & $Compiler @Arguments -o $Executable
    if ($LASTEXITCODE -ne 0) {
        throw "$Name compilation failed with exit code $LASTEXITCODE"
    }
    & $Executable
    if ($LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE"
    }
    [pscustomobject]@{ Test = $Name; Result = "PASS" }
}

try {
    $CommonFlags = @("-std=c++17", "-Wall", "-Wextra", "-Wpedantic")
    Invoke-CompileAndRun "cpu-verify-schedule" ($CommonFlags + @(
        "-I", (Join-Path $RepositoryRoot "cpuworkload\include"),
        (Join-Path $PSScriptRoot "verify_schedule_test.cpp")
    ))
    Invoke-CompileAndRun "gpu-verify-schedule" ($CommonFlags + @(
        "-I", (Join-Path $RepositoryRoot "gpuworkload\include"),
        (Join-Path $PSScriptRoot "verify_schedule_test.cpp")
    ))
    Invoke-CompileAndRun "cpu-config-contract" ($CommonFlags + @(
        "-I", (Join-Path $RepositoryRoot "cpuworkload\include"),
        (Join-Path $PSScriptRoot "cpu_config_contract_test.cpp"),
        (Join-Path $RepositoryRoot "cpuworkload\src\config.cpp"),
        (Join-Path $RepositoryRoot "cpuworkload\src\profile.cpp"),
        (Join-Path $RepositoryRoot "cpuworkload\src\utils.cpp")
    ))
    Invoke-CompileAndRun "gpu-config-contract" ($CommonFlags + @(
        "-I", (Join-Path $RepositoryRoot "gpuworkload\include"),
        (Join-Path $PSScriptRoot "gpu_config_contract_test.cpp"),
        (Join-Path $RepositoryRoot "gpuworkload\src\config.cpp"),
        (Join-Path $RepositoryRoot "gpuworkload\src\profile.cpp"),
        (Join-Path $RepositoryRoot "gpuworkload\src\utils.cpp")
    ))
} finally {
    $ResolvedTestDirectory = [IO.Path]::GetFullPath($TestDirectory)
    if (-not $ResolvedTestDirectory.StartsWith($TemporaryRoot, [StringComparison]::OrdinalIgnoreCase) -or
        $ResolvedTestDirectory -eq $TemporaryRoot) {
        throw "Refusing to remove unexpected test directory: $ResolvedTestDirectory"
    }
    Remove-Item -LiteralPath $ResolvedTestDirectory -Recurse -Force
}
