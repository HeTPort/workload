# NPU AVS Workload Development Plan

## Overview

The NPU benchmark should be implemented as `npuworkload/`, a third peer of
`gpuworkload/` and `cpuworkload/`. It should retain the repository's existing
project layout, configuration precedence, backend-factory pattern, JSONL event
lifecycle, result codes, timeout handling, and monitoring behavior. NPU-specific
differences should remain behind the backend and verification interfaces:
asynchronous inference submission, tensor input/output, model assets, device
partition reporting, and strict detection of CPU/GPU fallback.

The initial implementation should reuse the CPU project's common framework,
adopt the GPU project's submit/wait/readback execution pattern, and use
Test_tinypl primarily as a source of workload definitions and test assets. The
first complete workload should be KWS01; IC01, AD01, and SWW01 should follow
after the common runner and one real NPU backend are stable.

## Master summary

| Area | Recommended decision | Primary reuse | Key NPU addition | Acceptance condition |
|---|---|---|---|---|
| Project structure | Add an independent `npuworkload/` peer project | CPU directory and common-file layout | `npu_avs` namespace and NPU build options | NPU project builds without changing CPU/GPU behavior |
| Execution model | Use submit → wait → read-output semantics | GPU asynchronous backend lifecycle | Tensor input/output and inference completion | Null and hardware backends follow the same runner lifecycle |
| Configuration | Preserve profile → JSON → CLI precedence | CPU/GPU config and profile components | Model, workload, manifest, strict-NPU and fallback fields | Effective configuration is reproducible and validated |
| Resources | Reuse framework code, not device-specific protocol code | CPU common framework, GPU timing pattern, Test_tinypl workload definitions | Real models, official inputs, golden tensors and vendor SDK | Every asset has provenance, metadata and a hash |
| Correctness | Validate against an independent reference runtime | Existing verifier/event conventions | Tensor tolerance, task-level metrics and fallback validation | Incorrect output or unexpected fallback fails the run |
| Metrics | Preserve heartbeat and summary reporting | Existing JSONL logger and metrics code | Prepare, first-inference, warm-inference and device-time fields | Cold, warm, sustained and periodic results are distinguishable |
| AVS/DVFS | Keep platform control outside the portable runner | Existing external-monitor boundary | Requested/observed OPP and synchronized trace metadata | Each operating point includes correctness, power, thermal and performance data |
| Rollout | Close one workload before expanding coverage | Test_tinypl KWS01 shape and synthetic inputs | Restored KWS model, real samples and golden outputs | KWS passes reference, NPU, repeatability and no-fallback gates |

## Development plan

| Phase | Priority | Work | Main deliverables | Completion gate |
|---|---:|---|---|---|
| 0. Structural baseline | P0 | Create `npuworkload/` from the CPU project's common structure; rename namespaces and executable; retain only a null backend initially | CMake, build scripts, common headers/sources and `null` backend | Desktop build succeeds; `start`, `heartbeat`, and `summary` JSONL records are valid |
| 1. NPU configuration model | P0 | Add `workload`, `model`, `input_manifest`, tensor metadata, warm-up count, strict-NPU and fallback settings | `config.h/.cpp`, profiles and example JSON configurations | Config precedence remains profile → JSON → CLI; invalid combinations are rejected |
| 2. NPU backend contract | P0 | Implement `Init`, `CreateResources`, `SetInput`, `SubmitInference`, `WaitForCompletion`, `ReadOutput`, device timing and partition reporting | `backend.h`, `backend_factory.cpp` and null-backend tests | Null backend completes deterministic inference cycles and timeout tests |
| 3. Reference CPU backend | P0 | Add a reference inference runtime for functional validation and golden generation | `reference_cpu` backend | One model runs on desktop; repeated outputs are stable; golden outputs can be generated |
| 4. KWS01 workload closure | P0 | Restore the real KWS model, preprocessing definition, representative inputs, labels and golden tensors | Model manifest, smoke inputs, golden outputs and verifier | Reference backend passes all KWS smoke samples and a 100-repeat stability test |
| 5. First hardware NPU backend | P0 | Integrate the selected Android, HarmonyOS or vendor NPU runtime | Runtime backend, toolchain configuration and deployment instructions | Model executes successfully and partition information proves actual NPU use |
| 6. Correctness and fallback control | P0 | Compare output tensors with independent golden data; detect CPU/GPU fallback | Tensor verifier, fallback status fields and diagnostic dumps | Incorrect output and unexpected fallback produce a non-zero exit status |
| 7. Performance modes | P1 | Implement cold start, first inference, warm latency, sustained throughput and periodic execution | Profiles and separate timing fields | Results distinguish preparation, first invocation, warm invocation and end-to-end timing |
| 8. NPU metrics and telemetry | P1 | Add device time, inference rate, latency percentiles, deadline misses and runtime metadata | Extended heartbeat and summary schema | JSONL supports latency/throughput comparison without parsing vendor logs |
| 9. AVS/DVFS integration | P1 | Accept requested/observed voltage and frequency metadata; synchronize external power and thermal traces | AVS profiles, trace identifiers and run metadata | Every AVS point records correctness, latency, power, temperature and requested/observed OPP |
| 10. Additional workloads | P2 | Add IC01, AD01 and then SWW01 | Workload manifests, validators and profiles | Each workload passes reference and NPU smoke/accuracy gates |
| 11. Regression and release | P2 | Add build tests, timeout/error tests, golden-mismatch tests and long-duration stability tests | Smoke suite, user manual and design document | A fresh checkout can build, deploy and reproduce a documented result |

## Resource reuse

| Source resource | Reuse level | NPU use | Required adaptation |
|---|---|---|---|
| CPU project directory layout | High | Foundation for `npuworkload/` | Rename `cpu_avs` to `npu_avs`; replace compute backends |
| CPU common components | High | Config, logger, metrics, heartbeat, profiles, results, CRC and utilities | Change batch/operation terminology to inference terminology |
| CPU runner lifecycle | High | Warm-up, duration/count stopping, duty cycle, timeout and summary | Replace synchronous `RunBatch()` with submit/wait/read-output |
| GPU backend lifecycle | High | Asynchronous NPU execution | Map frame submission to inference submission and image readback to tensor readback |
| GPU device timestamps | Medium | Optional hardware/runtime timing | Implement only when the NPU runtime exposes trustworthy timestamps |
| CPU build scripts and presets | High | Desktop, Android and HarmonyOS builds | Add NPU SDK include paths, libraries and feature flags |
| Existing JSONL event model | High | `start`, `heartbeat`, `verify`, `golden`, `error`, and `summary` | Add NPU fields without renaming existing common fields |
| Existing result codes 0–7 | High | Stable automation contract | Report unexpected fallback as `API_ERROR` with a detailed reason |
| Test_tinypl tensor dimensions | High | Workload descriptions for KWS, IC, AD and SWW | Verify layout, dtype, quantization scale and zero point against actual models |
| Test_tinypl synthetic inputs | Medium | Smoke, stress and data-dependent power testing | Do not use them as accuracy datasets |
| Test_tinypl model declarations | Low | Symbol and intended-model references | Restore actual model binaries; declarations alone are insufficient |
| Test_tinypl `th_*` interface | Conceptual only | Identifies load, infer and result boundaries | Replace with the C++ backend interface |
| Test_tinypl UART protocol | None | Not required inside the new executable | Use file, shared memory or RPC transfer appropriate to the target |
| Test_tinypl Python serial runner | Low | Possible reference for result interpretation | Keep the existing C++ runner authoritative |
| Official/reference datasets | Required new resource | Accuracy measurement | Recover with provenance, licensing information and hashes |
| Vendor NPU SDK/runtime | Required new resource | Actual accelerator execution | Isolate entirely inside its backend directory |

## Points of architectural consistency

| Design point | GPU implementation | CPU implementation | Recommended NPU implementation |
|---|---|---|---|
| Project boundary | `gpuworkload/` | `cpuworkload/` | `npuworkload/` |
| Namespace | `gpu_avs` | `cpu_avs` | `npu_avs` |
| Executable | `gpu-avs-workload` | `cpu-avs-workload` | `npu-avs-workload` |
| Work unit | Frame | Batch | Inference |
| Primary asset | Shader | Compute kernel | Model |
| Backend selection | GLES, Vulkan, OpenCL | Integer, matrix, memory, mixed | Reference CPU, Android NPU, Harmony NPU, vendor runtime |
| Backend factory | `CreateBackend(cfg)` | `CreateBackend(cfg)` | `CreateBackend(cfg)` |
| Execution style | Submit → wait → readback | Run synchronous batch | Set input → submit → wait → read output |
| Configuration precedence | Profile → JSON → CLI | Profile → JSON → CLI | Profile → JSON → CLI |
| Warm-up | Frames | Batches/time | Inference count/time |
| Stop conditions | Duration or frames | Duration or batches | Duration or inferences |
| Correctness | Image/buffer golden | Deterministic checksum | Independent golden tensor with tolerance |
| Live metric | Frames/s | Operations/s | Inferences/s |
| Latency metric | Frame/GPU time | Batch time | Host, runtime and device inference time |
| Timeout | Frame/global | Batch/global | Inference/global |
| Verification event | Readback comparison | Checksum comparison | Tensor comparison and output digest |
| Fallback detection | API selection is explicit | Not applicable | Mandatory partition/device verification |
| Output format | JSONL lifecycle | JSONL lifecycle | Same JSONL lifecycle with additional NPU fields |
| Build model | Feature-gated APIs | Backend source list | Feature-gated runtime backends |
| Tests | Null/API smoke tests | Backend and failure smoke tests | Null, reference, golden mismatch, timeout and fallback tests |

## Recommended first implementation slice

1. Copy the CPU structural skeleton into `npuworkload/` and rename it to
   `npu_avs`.
2. Replace `ICpuBackend` with a GPU-like asynchronous `INpuBackend`.
3. Implement `null` and `reference_cpu` backends.
4. Restore the KWS01 model, representative inputs and independent golden
   outputs.
5. Add one real NPU backend and prove that it executes without CPU/GPU
   fallback.
6. Add AVS/DVFS profiles only after correctness and timing boundaries are
   stable.
