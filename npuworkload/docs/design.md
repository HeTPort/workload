# NPU Workload Baseline Design

## Delivered scope

The implementation creates `npuworkload/` as an independent peer of the CPU and GPU projects, separates workload profiles from runtime backends, changes the runner boundary from synchronous batches to asynchronous tensor-set inference, supplies `null` plus `reference_cpu` framework backends, and makes a versioned manifest the source of the runtime tensor contract and canonical input bytes.

The project preserves profile -> JSON -> CLI configuration precedence, result codes 0–7, JSONL `start`/`heartbeat`/`verify`/`golden`/`error`/`summary` records, global and per-inference timeouts, and periodic monitoring.

## Backend contract

`INpuBackend` owns initialization and resources, receives a `ProfileSpec`, accepts an ordered typed `TensorSet` through `SetInputs`, starts work through `SubmitInference`, waits with a bounded timeout, and returns an `InferenceResult` through `ReadOutputs`. Results separate host latency from optional backend-reported device time. `BackendExecutionInfo` carries runtime, execution target, partition descriptions, and fallback state for future hardware validation.

The lifecycle is stateful: new inputs cannot replace pending work, a second submission cannot overlap unread outputs, and outputs cannot be read before completion. Tensor order, names, dtype, shape, layout and quantization must match the selected profile. Cancellation is cooperative and teardown has a bounded grace period; a copied task closure can finish safely after detachment without accessing a destroyed backend.

## Initial backends

- `null` asynchronously produces descriptor-correct zero outputs and reports target `none`.
- `reference_cpu` asynchronously executes a deterministic multi-input/multi-output framework transform and reports target `cpu_reference`.

The reference transform has no external model/runtime dependency. Its purpose is runner and tensor-contract validation. It is not an accuracy oracle, a substitute for the independent reference runtime, or a source of authoritative KWS01 goldens.

## Profiles

`kws01`, `ic01`, `ad01`, and `sww01` are registered as workload profiles and remain independent of backend selection. They are intentionally unsupported until exact manifests and executors exist. `framework_smoke` is an explicit test-only profile with two inputs and two outputs; its checked-in manifest activates the profile and both included backends support it.

## Manifest and assets

The scalar configuration continues to select the profile and backend. `input_manifest` points to the versioned profile manifest, which supplies the exact ordered tensor descriptors and raw input assets used by every backend. Its tensor signature must match the approved registry contract; a manifest cannot activate an unfinished profile or redefine an executor interface. The loader rejects duplicate JSON keys, unknown schema fields, invalid UTF-8, unsupported schema/profile identities, invalid tensor metadata, duplicate tensor names, byte-count mismatches, SHA-256 mismatches, and any path that resolves outside the declared asset root. It computes a SHA-256 of the manifest bytes and a canonical tensor-signature SHA-256 for reproducibility.

The checked-in `framework_smoke` fixture is intentionally model-free and has no authoritative output goldens. Production profiles require provenance-recorded model and sample assets plus independently generated goldens before they can be called implemented.

## Verification

Checksums cover the complete ordered output tensor set, including descriptor identity and payload bytes. An explicit golden checksum is authoritative. Without one, the first measured output becomes a run-local baseline, so subsequent inferences detect nondeterminism only. `--generate-golden` emits a candidate checksum for framework testing; it does not validate the generating implementation.

## Deferred work

Real profile model integration, golden-tensor tolerance comparison in the verifier, KWS01 assets and executor, logging-schema v2, strict hardware-NPU enforcement, vendor runtime integration, and AVS/DVFS metadata are intentionally deferred to subsequent slices.
