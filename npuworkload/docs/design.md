# NPU Workload Baseline Design

## Delivered scope

The first implementation slice creates `npuworkload/` as an independent peer of the CPU and GPU projects, changes the runner boundary from synchronous batches to asynchronous tensor inference, and supplies `null` plus `reference_cpu` backends.

The project preserves profile -> JSON -> CLI configuration precedence, result codes 0–7, JSONL `start`/`heartbeat`/`verify`/`golden`/`error`/`summary` records, global and per-inference timeouts, and periodic monitoring.

## Backend contract

`INpuBackend` owns initialization and resources, accepts a typed tensor through `SetInput`, starts work through `SubmitInference`, waits with a bounded timeout, and returns an `InferenceResult` through `ReadOutput`. Results separate host latency from optional backend-reported device time. `BackendExecutionInfo` carries runtime, execution target, partition descriptions, and fallback state for future hardware validation.

The lifecycle is stateful: a new input cannot replace pending work, a second submission cannot overlap unread output, and output cannot be read before completion.

## Initial backends

- `null` asynchronously returns the input tensor and reports target `none`.
- `reference_cpu` asynchronously executes a deterministic fixed-point dense transform over int8 input and reports target `cpu_reference`.

The reference transform has no external model/runtime dependency. Its purpose is runner validation and reproducible golden-checksum generation. It is not a substitute for the independent reference runtime and real KWS01 assets required by later phases.

## Verification

Checksums cover output tensor bytes. An explicit golden checksum is authoritative. Without one, the first measured output becomes a run-local baseline, so subsequent inferences detect nondeterminism. `--generate-golden` emits the deterministic checksum for capture by a later asset pipeline.

## Deferred work

Real model loading, manifest-driven inputs, tensor tolerance comparison, KWS01 assets, strict hardware-NPU enforcement, vendor runtime integration, and AVS/DVFS metadata are intentionally deferred to subsequent slices.
