# CPU AVS Workload

This repository now contains an additive portable CPU workload under `cpu/`, alongside the original GPU benchmark sources. The CPU tool provides deterministic integer, floating-point, matrix, memory, mixed, and idle backends with GPU-compatible JSONL monitoring events.

Quick desktop build on Windows:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
.\build.ps1 -Target desktop -Configuration Release
.\build\desktop-release\cpu-avs-workload.exe --config .\configs\cpu_mixed.json
```

Documentation:

- [Design](docs/design.md)
- [User manual](docs/user_manual.md)

Automated smoke test:

```powershell
.\tests\smoke.ps1 -Executable .\build\desktop-release\cpu-avs-workload.exe
```
