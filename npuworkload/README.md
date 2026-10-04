# NPU Workload Generator

This project repeatedly invokes real NPU models through a common workload/profile contract. It includes four MLCommons Tiny profiles with checked TensorFlow Lite models and model-ready input tensors:

| Profile | Workload | Input | Output |
|---|---|---|---|
| `kws01` | Keyword spotting | int8 `[1,49,10,1]` | int8 `[1,12]` |
| `ic01` | Image classification | int8 `[1,32,32,3]` | int8 `[1,10]` |
| `ad01` | Anomaly detection | int8 `[1,640]` | int8 `[1,640]` |
| `sww01` | Streaming-wakeword model window | int8 `[1,30,1,40]` | int8 `[1,3]` |

The scope is deliberately small: generate repeatable inference load, enforce consistent tensors, and report lifecycle/timing results. It does not control AVS/DVFS, collect power or thermal traces, or implement separate cold/warm/sustained/periodic scenario modes. Warm-up count, duration, inference count, and per-inference timeout already provide the controls needed for load generation.

## Build and host conformance

```powershell
./build.ps1
./tests/smoke.ps1
```

The `null` backend checks a profile and its assets without requiring a device runtime:

```powershell
./build/desktop-release/npu-avs-workload.exe `
  --profile kws01 --backend null `
  --input-manifest ./profiles/kws01/manifest.json `
  --duration 0 --inferences 10 --warmup-inferences 0
```

## NPU execution

`tflite_delegate` dynamically loads the public TensorFlow Lite C API and its external-delegate loader. Supply the TensorFlow Lite runtime library and the target vendor's external-delegate library:

```powershell
./build/desktop-release/npu-avs-workload.exe `
  --profile kws01 --backend tflite_delegate `
  --input-manifest ./profiles/kws01/manifest.json `
  --runtime-library C:/runtime/tensorflowlite_c.dll `
  --delegate-library C:/runtime/vendor_npu_delegate.dll `
  --delegate-options "key=value;other=value" `
  --duration 60 --warmup-inferences 1
```

Both libraries are required. Delegate creation, interpreter creation, tensor allocation, or invocation failure stops the run; the tool never retries without the delegate. A delegate can still partition unsupported operators according to its own policy, so vendor tooling is the authority for proving complete NPU placement.

The backend lifecycle is `Init -> CreateResources -> SetInputs -> SubmitInference -> WaitForCompletion -> ReadOutputs -> Destroy`. Every backend receives the same ordered tensor descriptors and bytes from the selected manifest. `framework_smoke` and `reference_cpu` remain test-only framework fixtures.

Models and samples are pinned to MLCommons Tiny commit `4addd0fa08d216e20637637874e084895f289da4`. See `third_party/mlcommons-tiny/PROVENANCE.json` and `LICENSE.md`. The standard-library-only importer at `tools/import_mlcommons_profiles.py` reproducibly checks upstream hashes and recreates the checked assets.

See [docs/design.md](docs/design.md) for architecture and [docs/manifest.md](docs/manifest.md) for the manifest schema.
