# NPU AVS Workload

This project is the dependency-free contract foundation for NPU AVS workloads. It provides the common runner, configuration precedence, JSONL lifecycle, workload-profile registry, strict profile manifests, checked binary assets, an ordered multi-tensor backend interface, and deterministic `null` and `reference_cpu` framework backends. It does not yet integrate a vendor NPU runtime or a real model.

Build on Windows with `./build.ps1`, or use CMake directly. When CMake is unavailable, the PowerShell script falls back to a C++17 `g++` build for the desktop target.

Quick smoke run:

```powershell
./build.ps1
./build/desktop-release/npu-avs-workload.exe --profile framework_smoke --backend reference_cpu --input-manifest ./profiles/framework_smoke/manifest.json --duration 0 --inferences 5 --warmup-inferences 1
./tests/smoke.ps1
```

The backend lifecycle is `Init(profile) -> CreateResources -> SetInputs -> SubmitInference -> WaitForCompletion -> ReadOutputs -> Destroy`. The runner obtains the active tensor contract and input bytes from `--input-manifest`; it does not synthesize workload inputs. Inputs and outputs are ordered tensor sets validated against that contract. Asynchronous work uses bounded cancellation and teardown; a timeout does not perform an unbounded future join.

`kws01`, `ic01`, `ad01`, and `sww01` are registered workload identities. They intentionally return `UNSUPPORTED_PROFILE` until their real manifests, assets, and backend profile executors are added. `framework_smoke` has a repository-owned manifest and two hashed input fixtures; it exercises two inputs and two outputs with both included backends. The `reference_cpu` implementation remains a deterministic framework test, not an accuracy reference or NPU performance result.

See [docs/design.md](docs/design.md) for the delivered design and current limitations, and [docs/manifest.md](docs/manifest.md) for the manifest contract.
