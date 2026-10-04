# NPU Workload Generator Development Plan

## Objective

Implement the original NPU workload logic as runtime-neutral profiles while keeping the backend architecture consistent with the CPU/GPU projects. The tool's job is to generate repeatable model inference load. It is not an AVS controller, a power/thermal collector, or a benchmark-scenario orchestrator.

## Architecture

- A profile owns workload identity, model provenance, ordered tensor descriptors, quantization, and model-ready input assets.
- A backend owns runtime loading, delegate creation, model/interpreter resources, input binding, invocation, waiting, output capture, and execution metadata.
- The runner owns lifecycle, warm-up count, duration/inference stop conditions, deadlines, heartbeat, repeatability checking, metrics, and JSONL.
- Backends may not change a profile's input/output data types or formats.

The public tensor contract is `TensorSet`: an ordered array of named buffers described by dtype, shape, layout, quantization mode/scales/zero points/axis, and contiguous raw bytes. Manifest and registry tensor signatures must match before backend initialization.

## Delivered phases

1. Contract foundation — profile/backend separation, ordered multi-input/output tensors, checked byte sizes, stateful async lifecycle, and bounded teardown.
2. Strict manifests — dependency-free JSON parsing, checked model/input hashes, confined asset paths, provenance, and stable tensor-signature hashes.
3. Original profiles — KWS01, IC01, AD01, and SWW01 use pinned MLCommons Tiny TensorFlow Lite models and canonical model-ready input tensors.
4. Backend consistency — descriptor-driven `null` supports every profile; `reference_cpu` remains a framework-only test; `tflite_delegate` provides one generic hardware path through the TensorFlow Lite external-delegate interface and refuses missing delegate/runtime libraries.
5. Conformance — contract tests load every manifest and execute every profile through `null`; smoke tests cover repeatability, failure mapping, timeouts, and the required-runtime error path.

## Completion gates

- Every profile has a checked model, checked input, exact descriptors, and recorded upstream license/commit.
- Every manifest descriptor matches the built-in profile registry exactly.
- Every profile completes through the descriptor-driven backend without profile-specific runner logic.
- Real inference requires an external delegate; the backend does not silently retry on CPU.
- Host build and smoke/contract suites pass with no compiler warnings.
- Documentation distinguishes host conformance, repeated-output verification, and actual NPU execution.

## Explicitly excluded

- AVS/DVFS requested/observed operating points.
- Power, temperature, and trace metadata.
- Separate cold, warm, sustained, or periodic scenario objects. Equivalent load behavior is already expressible with `warmup_inferences`, `duration`, `inferences`, and repeated tool invocation.
- End-to-end audio/image preprocessing pipelines. Checked assets enter at the model tensor boundary so that load generation measures model execution rather than host feature extraction.
- Accuracy certification and independent golden generation.

## Device integration boundary

To run on a specific NPU, provide a TensorFlow Lite C runtime that exports the external-delegate API, the vendor delegate shared library, and any vendor delegate options. If a platform does not expose a TensorFlow Lite external delegate, add a backend adapter for that concrete runtime while preserving the existing profile descriptors and `INpuBackend` lifecycle. No other core work is required for the generic load-generator scope.
