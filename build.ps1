# 与 Linux 使用同一套参数，例如 .\build.ps1 --components cpu,npu --target host --engine direct --test
$ErrorActionPreference = 'Stop'
$taskPython = if ($env:PYTHON) { $env:PYTHON } else { 'python' }
$taskForward = @()
foreach ($taskArg in $args) { $taskForward += $(if ($taskArg -is [array]) { $taskArg -join ',' } else { $taskArg }) }
& $taskPython (Join-Path $PSScriptRoot 'scripts/build.py') @taskForward
exit $LASTEXITCODE
