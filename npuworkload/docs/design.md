# NPU Workload Generator Design

## Scope

The program generates repeatable NPU inference load. `kws01`, `ic01`, `ad01`, and `sww01` are model profiles; `null`, `reference_cpu`, and `tflite_delegate` are execution backends. Profile identity never doubles as backend selection.

AVS/DVFS operating points, power/thermal traces, and dedicated performance-scenario state machines are outside this project. Existing duration, inference-count, warm-up, heartbeat, and timeout fields are sufficient to shape a load run without adding hardware-management policy to the runner.

## Common contract

The strict manifest supplies the checked model, canonical model-ready inputs, ordered input/output descriptors, quantization, and provenance. The registry contains the same descriptors and rejects a manifest whose tensor signature differs. All backends therefore use identical tensor names, dtypes, shapes, layouts, quantization, and raw byte ordering.

The lifecycle is asynchronous: `Init`, `CreateResources`, `SetInputs`, `SubmitInference`, `WaitForCompletion`, `ReadOutputs`, and `Destroy`. A second inference cannot overlap unread output. Timeouts request cancellation and teardown has a bounded grace period; task closures retain their runtime state if a vendor invocation cannot stop immediately.

## Backends

- `null` produces descriptor-correct zero outputs for every implemented profile. It validates manifests, assets, runner behavior, and multi-profile backend consistency without claiming inference correctness.
- `reference_cpu` is the deterministic `framework_smoke` test transform only.
- `tflite_delegate` loads a TensorFlow Lite C shared library at runtime, requires a vendor external-delegate shared library, validates the model's runtime tensor types/shapes/sizes, copies canonical inputs, invokes the delegated interpreter, and captures raw outputs. It does not enable TensorFlow Lite's delegate-fallback option and never creates a CPU-only retry interpreter.

The generic external-delegate interface avoids hard-coding Android, HarmonyOS, or one vendor SDK. A concrete device run still needs compatible runtime/delegate libraries and any vendor-specific `key=value` options. Because TensorFlow Lite delegates may accept only part of a graph, this backend truthfully reports `external_npu_delegate`; full NPU partition proof must come from the selected delegate/runtime diagnostics.

## Profiles and data

The checked profiles are pinned to MLCommons Tiny commit `4addd0fa08d216e20637637874e084895f289da4` under Apache-2.0. Inputs are already quantized for each model. KWS01 contains an MFCC tensor, IC01 contains the reference image tensor after the required unsigned-to-signed conversion, AD01 contains one quantized log-mel feature window, and SWW01 contains one quantized spectrogram window. SWW01 intentionally measures model invocation rather than microphone capture, rolling state, or end-to-end wakeword logic.

No independent output goldens are claimed. With no configured golden checksum, verification is repeatability within a run, not accuracy certification. The goal here is stable load generation; accuracy validation belongs in a separate model-validation workflow.

## Execution evidence and timing

JSONL preserves the common start, heartbeat, inference, verify, error, and summary records. Host inference time includes the TensorFlow Lite invocation call. The generic C API does not expose vendor device-only time, so `device_time_valid` remains false. `fallback_used=false` means the runner did not initiate a fallback; it is not proof that every graph node was delegated.
