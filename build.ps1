param(
    [ValidateSet("desktop", "android-arm64", "harmony-arm64")]
    [string]$Target = "desktop",
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDirectory = Join-Path $ProjectRoot ("build\" + $Target + "-" + $Configuration.ToLowerInvariant())

if ($Clean -and (Test-Path -LiteralPath $BuildDirectory)) {
    $ResolvedBuild = [System.IO.Path]::GetFullPath($BuildDirectory)
    $ResolvedRoot = [System.IO.Path]::GetFullPath((Join-Path $ProjectRoot "build"))
    if (-not $ResolvedBuild.StartsWith($ResolvedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clean outside the project build directory: $ResolvedBuild"
    }
    Remove-Item -LiteralPath $ResolvedBuild -Recurse -Force
}

$CMake = Get-Command cmake -ErrorAction SilentlyContinue

if ($Target -eq "desktop" -and -not $CMake) {
    $Compiler = Get-Command g++ -ErrorAction SilentlyContinue
    if (-not $Compiler) {
        throw "Neither cmake nor g++ is available on PATH. Install one or select an NDK target."
    }

    New-Item -ItemType Directory -Path $BuildDirectory -Force | Out-Null
    $Sources = @(
        "cpu/src/main.cpp",
        "cpu/src/backend_factory.cpp",
        "cpu/src/config.cpp",
        "cpu/src/crc32.cpp",
        "cpu/src/heartbeat.cpp",
        "cpu/src/logger.cpp",
        "cpu/src/metrics.cpp",
        "cpu/src/profile.cpp",
        "cpu/src/runner.cpp",
        "cpu/src/utils.cpp",
        "cpu/src/verifier.cpp",
        "cpu/src/backends/null/null_backend.cpp",
        "cpu/src/backends/integer/integer_backend.cpp",
        "cpu/src/backends/floating_point/floating_point_backend.cpp",
        "cpu/src/backends/matrix/matrix_backend.cpp",
        "cpu/src/backends/memory/memory_backend.cpp",
        "cpu/src/backends/mixed/mixed_backend.cpp"
    ) | ForEach-Object { Join-Path $ProjectRoot $_ }

    $Output = Join-Path $BuildDirectory "cpu-avs-workload.exe"
    $Optimization = if ($Configuration -eq "Release") { "-O2" } else { "-O0" }
    $Arguments = @(
        "-std=c++17", $Optimization, "-Wall", "-Wextra", "-Wpedantic",
        "-static-libgcc", "-static-libstdc++",
        "-I", (Join-Path $ProjectRoot "cpu/include"),
        "-I", (Join-Path $ProjectRoot "cpu/src"),
        "-o", $Output
    ) + $Sources

    & $Compiler.Source @Arguments
    if ($LASTEXITCODE -ne 0) { throw "g++ build failed with exit code $LASTEXITCODE" }
    Write-Host "Built $Output"
} else {
    if (-not $CMake) {
        throw "cmake is required for Android and HarmonyOS builds."
    }

    $ConfigureArguments = @(
        "-S", $ProjectRoot,
        "-B", $BuildDirectory,
        "-DCMAKE_BUILD_TYPE=$Configuration",
        "-DCPU_AVS_STATIC_CXX_RUNTIME=ON"
    )

    if ($Target -eq "android-arm64") {
        if (-not $env:ANDROID_NDK_HOME) { throw "Set ANDROID_NDK_HOME to the Android NDK root." }
        $Toolchain = Join-Path $env:ANDROID_NDK_HOME "build\cmake\android.toolchain.cmake"
        if (-not (Test-Path -LiteralPath $Toolchain)) { throw "Android toolchain not found: $Toolchain" }
        $ConfigureArguments += @(
            "-DCMAKE_TOOLCHAIN_FILE=$Toolchain",
            "-DANDROID_ABI=arm64-v8a",
            "-DANDROID_PLATFORM=android-24",
            "-DANDROID_STL=c++_static"
        )
    }

    if ($Target -eq "harmony-arm64") {
        if (-not $env:OHOS_NDK_HOME) { throw "Set OHOS_NDK_HOME to the HarmonyOS NDK root." }
        $Toolchain = Join-Path $env:OHOS_NDK_HOME "build\cmake\ohos.toolchain.cmake"
        if (-not (Test-Path -LiteralPath $Toolchain)) { throw "HarmonyOS toolchain not found: $Toolchain" }
        $ConfigureArguments += @(
            "-DCMAKE_TOOLCHAIN_FILE=$Toolchain",
            "-DOHOS_ARCH=arm64-v8a",
            "-DOHOS_STL=c++_static"
        )
    }

    & $CMake.Source @ConfigureArguments
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }
    & $CMake.Source --build $BuildDirectory --config $Configuration --parallel
    if ($LASTEXITCODE -ne 0) { throw "CMake build failed with exit code $LASTEXITCODE" }
    Write-Host "Built target $Target in $BuildDirectory"
}
