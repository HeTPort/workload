# NPU AVS Workload

This project is the dependency-free structural baseline for NPU AVS workloads. It provides the common runner, configuration precedence, JSONL lifecycle, asynchronous backend interface, and deterministic `null` and `reference_cpu` backends. It does not yet integrate a vendor NPU runtime or a real model.

Build on Windows with `./build.ps1`, or use CMake directly. When CMake is unavailable, the PowerShell script falls back to a C++17 `g++` build for the desktop target.

Quick smoke run:

```powershell
./build.ps1
./build/desktop-release/npu-avs-workload.exe --profile reference --duration 0 --inferences 5 --warmup-inferences 1
./tests/smoke.ps1
```

The backend lifecycle is `Init -> CreateResources -> SetInput -> SubmitInference -> WaitForCompletion -> ReadOutput`. Both included backends implement submission through an asynchronous future. `reference_cpu` uses a portable deterministic int8 dense transform and is intended for framework validation and later golden generation—not as an NPU performance result.

See [docs/design.md](docs/design.md) for the delivered design and current limitations.
