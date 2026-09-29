# NPU AVS Workload Development Plan

## 1. Objective and architectural decision

The NPU project remains an independent peer of `cpuworkload/` and
`gpuworkload/`. It reuses their configuration precedence, result codes, JSONL
lifecycle, timing units, timeout behavior, and monitoring conventions.

The original NPU workloads are implemented as named profiles:

- `kws01`
- `ic01`
- `ad01`
- `sww01`

A profile is a workload identity and data contract. A backend is an execution
implementation. `reference_cpu`, Android/Harmony runtimes, and vendor runtimes
are therefore backends, not profiles. Every backend advertises which profiles
it implements and creates a profile-specific executor internally.

The canonical profile description remains runtime-neutral so that every
backend receives exactly the same named tensors and must return exactly the
same named output contract. Runtime-specific model loading, compilation,
submission, synchronization, and partition inspection remain inside the
backend. Shared input decoding, canonical preprocessing, task-level
postprocessing, and verification must not be independently reimplemented by
each backend, because doing so would make cross-backend comparison ambiguous.

Performance scenarios are orthogonal to workload identity. Use a separate
`scenario` value such as `cold`, `warm`, `sustained`, or `periodic`; do not
create backend or timing aliases in the profile namespace.

## 2. Required architecture

| Component | Owns | Must not own |
|---|---|---|
| Profile registry | Profile ID, manifest path, tensor signatures, preprocessing version, task semantics, default run parameters and optional default backend | Runtime handles or vendor-specific options |
| Shared profile I/O | Sample loading, canonical preprocessing, input tensor construction, golden loading and task-level decoding | Runtime compilation or device selection |
| Backend factory | Selection of `null`, `reference_cpu`, Android/Harmony, or vendor backend | Workload identity |
| Backend | Runtime initialization, supported-profile query, creation of a profile executor, model compilation, input binding, submission, wait, output retrieval and execution evidence | Changes to canonical tensor names, shapes, dtypes, layout or quantization |
| Backend profile executor | Runtime-specific implementation of one profile, for example `reference_cpu/kws01` | Backend-independent verification rules |
| Runner | Lifecycle, deadlines, warm-up, measurement, heartbeat, metrics, result mapping and logging | Synthetic workload construction or model-specific tensor assumptions |
| Verifier | Independent golden comparison, tolerance rules, repeatability checks and task metric checks | Treating the first device result as proof of correctness |

Recommended source organization:

```text
npuworkload/
  profiles/                       # Runtime-neutral profile manifests
    kws01.json
    ic01.json
    ad01.json
    sww01.json
  src/profile_registry.*
  src/profile_io/                 # Shared decoding/pre/postprocessing
  src/backends/null/profiles/
  src/backends/reference_cpu/profiles/
  src/backends/<runtime>/profiles/
  assets/<profile>/               # Models, samples, labels, goldens, hashes
```

The command line remains explicit, for example:

```text
npu-avs-workload --profile kws01 --backend reference_cpu --scenario warm
npu-avs-workload --profile kws01 --backend android_nnapi --scenario sustained
```

A profile may supply a default backend for convenience, but `--backend` is an
independent override. An unsupported profile/backend pair is rejected before
resource creation with `API_ERROR` and `UNSUPPORTED_PROFILE`.

## 3. Normative public data contract

### 3.1 Common scalar types and units

The public configuration and JSONL formats use the same conventions as the
CPU/GPU projects unless an NPU-specific type is required.

| Meaning | C++ type | JSON type and unit |
|---|---|---|
| Profile, backend, API, mode, scenario | `std::string` | string |
| Duration and global timeout | `double` | finite number, seconds |
| Host, runtime and device latency | `double` | finite number, milliseconds |
| Inference, warm-up, operation and mismatch counts | `uint64_t` | non-negative integer |
| Per-inference timeout | `uint32_t` | non-negative integer, milliseconds |
| Flags | `bool` | JSON boolean, never `0`/`1` or a string |
| Exit/result status | existing `ResultCode` | stable codes 0–7 and existing result strings |
| Paths, hashes and runtime identifiers | `std::string` | UTF-8 string |

All narrowing conversions are range checked. NaN and infinity are rejected.
The effective configuration remains profile defaults → JSON configuration →
CLI overrides.

### 3.2 Tensor types

Separate metadata from storage:

```text
TensorSpec
  name: string
  data_type: TensorDataType
  shape: vector<uint32_t>
  layout: TensorLayout
  quantization: QuantizationSpec

TensorBuffer
  spec: TensorSpec
  data: vector<uint8_t>
```

The stable serialized dtype names are `int8`, `uint8`, `int16`, `int32`,
`float16`, and `float32`. A backend may support a subset, but must report an
unsupported dtype rather than reinterpret it.

The initial stable layout names are `scalar`, `nc`, `nchw`, `nhwc`, `ntc`, and
`raw`. Data is contiguous row-major in the declared layout with no implicit
padding or stride. Multi-byte elements are little-endian; floating-point values
use IEEE-754 representation.

Quantization is one of:

- `none`
- `per_tensor`: one finite positive `float32` scale and one `int32` zero point
- `per_axis`: equal-length scale and zero-point arrays plus a valid axis

Tensor names are non-empty and unique within each input or output list. Tensor
order is the manifest order and is preserved across every backend. Shapes have
positive dimensions, bounded rank and bounded byte size. Element-count and
byte-count multiplication is checked for overflow before allocation or access.

The public backend contract uses collections for both directions, even when a
profile currently has one input and one output:

```text
TensorSet = vector<TensorBuffer>
SetInputs(const TensorSet&)
ReadOutputs(TensorSet&)
```

### 3.3 Profile manifest and binary data

Complex profile data is stored in a standards-compliant JSON manifest, not in
the scalar configuration parser. The manifest has a schema version and contains:

- profile ID and semantic version;
- model format, model file, SHA-256 and provenance;
- ordered input and output `TensorSpec` arrays;
- preprocessing/postprocessing version and parameters;
- sample IDs, raw tensor files and SHA-256 values;
- independent golden tensor files and tolerance policy;
- labels and task-level expected results when applicable.

Raw tensor files contain only canonical tensor bytes. Their dtype, shape,
layout, quantization and hash are supplied by the manifest. The loader rejects
unknown fields, duplicate tensor names, byte-count mismatches, hash mismatches,
unsupported schema versions, and paths escaping the asset root.

Configuration JSON may remain scalar-only in the short term, but manifests
must use a real JSON parser and schema validation. Regex extraction is not an
acceptable manifest parser.

### 3.4 Backend lifecycle and status

The target lifecycle is:

```text
Init(config, profile_spec)
QuerySupport(profile_spec)
CreateResources()
SetInputs(tensor_set)
SubmitInference(index)
WaitForCompletion(deadline)
ReadOutputs(inference_result)
Destroy(deadline)
```

All fallible lifecycle methods return `BackendStatus`; do not mix `bool` and
status enums. `InferenceResult` contains an ordered output `TensorSet`,
`uint64_t operation_count`, optional device time, and execution evidence.

Timeout is a hard contract. A timed-out inference must be cancelled when the
runtime supports cancellation, or safely abandoned without an unbounded join.
`Destroy` must complete within a documented grace period. The per-inference
deadline is the smaller of `inference_timeout_ms` and the remaining global
deadline.

`BackendExecutionInfo` must report evidence, not only a backend assertion:
runtime/provider version, selected device identifiers, compiled partitions,
partition-to-device assignment, fallback policy, and observed fallback. In
strict-NPU mode, missing evidence or any CPU/GPU partition fails the run.

### 3.5 Verification and JSONL

Correctness and repeatability are separate results:

- correctness compares every output tensor with independent goldens using the
  profile's exact/tolerance policy and task-level expectations;
- repeatability compares repeated outputs only after correctness has passed;
- generated goldens are emitted as candidate artifacts and cannot validate the
  run that generated them;
- the first measured backend output is never treated as an independent golden.

Keep the common JSONL record types and common fields used by CPU/GPU:
`start`, `heartbeat`, `inference`, `verify`, `golden`, `error`, and `summary`.
When the tensor/profile contract lands, increment the NPU schema version and
add `profile_version`, `scenario`, `manifest_sha256`, `model_sha256`,
`tensor_signature_sha256`, verification policy, runtime/provider version,
device IDs, partition evidence, fallback policy, and observed fallback.

`first_inference_time_ms` means a true cold first inference before warm-up.
Warm/steady-state timing is reported separately. A warm-up heartbeat reports
cumulative, not last-inference, operation count.

## 4. Review of the first three delivered stages

The three stages are a useful framework prototype, but they do not yet satisfy
the profile architecture or correctness contract above.

| Stage | What is sound | Issue to resolve | Priority |
|---|---|---|---:|
| 1. Project skeleton/common framework | Independent peer project, namespace, build, profile → JSON → CLI precedence, result mapping, JSONL lifecycle | Current profiles are `null` and `reference`, so profile identity is conflated with backend selection | P0 |
| 1. Project skeleton/common framework | Scalar type parsing is range checked and common time/count conventions are mostly retained | Runner ignores the input manifest and always constructs one synthetic int8 tensor | P0 |
| 1. Project skeleton/common framework | Configuration rejects many invalid scalar values | Regex JSON parsing cannot represent or strictly validate tensor arrays, shapes, quantization or multi-I/O manifests | P0 |
| 2. Async backend contract | Explicit set → submit → wait → read lifecycle and state checks are a good basis | Contract exposes only one input and one output; the common async base also hardcodes int8 and `input_elements` | P0 |
| 2. Async backend contract | Backend status distinguishes timeout/allocation/unknown failures during submission and wait | Other lifecycle calls return `bool`, losing precise status and making error mapping inconsistent | P1 |
| 2. Async backend contract | Per-inference wait uses a timeout | Timeout leaves a valid future and `Destroy()` performs an unbounded `get()`, so a timeout can still hang shutdown | P0 |
| 2. Async backend contract | Global and inference timeout settings exist | The submitted wait receives the full per-inference timeout rather than the remaining global deadline | P0 |
| 2. Tensor utility | Shape-product overflow is checked | Product of element count and element size is not checked before byte-size comparison | P1 |
| 3. Null backend | Useful deterministic lifecycle test double | It returns the input as output and therefore does not validate the selected profile's output descriptors | P1 |
| 3. Reference CPU backend | Useful deterministic framework self-test | It is a synthetic dense transform, not a reference implementation of KWS01/IC01/AD01/SWW01 and must not generate authoritative goldens | P0 |
| 3. Verification | Explicit supplied checksum is honored | Without a supplied checksum, the first backend output becomes the baseline; this detects nondeterminism but can certify an incorrect implementation | P0 |
| 3. Execution metadata | Fields exist for runtime, target, partitions and fallback | Values are self-reported strings; there is no provider/device/partition evidence sufficient to prove NPU-only execution | P0 before hardware claims |
| 3. Metrics | Host/device latency, throughput, heartbeat and summary are present | `first_inference_time_ms` is taken after warm-up and warm-up heartbeat operations are not cumulative | P1 |
| 3. Tests | PASS, checksum failure, timeout, unsupported backend and event presence are exercised | Missing state-machine, bounded-timeout cleanup, multi-I/O, tensor-signature, independent-golden, manifest and strict-fallback tests | P0/P1 |

The synthetic `reference_cpu` implementation should be renamed or clearly kept
as a framework-test backend until the first real profile executor exists. It is
not evidence that phase 3, as originally named, is complete.

## 5. Revised development sequence

| Phase | Priority | Work and deliverables | Completion gate |
|---|---:|---|---|
| 0. Baseline reconciliation | P0 | Keep the current buildable prototype; record the review findings; rename synthetic semantics where necessary | Existing CPU/GPU behavior is unchanged; prototype tests remain green |
| 1. Contract foundation | P0 | Add `ProfileSpec`, `TensorSpec`, layout/quantization, ordered `TensorSet`, uniform `BackendStatus`, support query, deadline-aware wait/destroy and profile/backend separation | Contract unit tests cover multi-I/O, invalid states, overflow and bounded timeout cleanup |
| 2. Manifest and profile registry | P0 | Add strict manifest parser/schema, canonical asset loader, `kws01`/`ic01`/`ad01`/`sww01` registry entries and separate scenarios | `--list-profiles` lists workload IDs; every manifest produces a stable tensor-signature hash |
| 3. Descriptor-driven test backend | P0 | Make `null` support every registered profile using descriptor-correct deterministic outputs; retain synthetic compute only as a named test backend | Conformance suite runs every profile signature through the test backend without special cases |
| 4. KWS01 reference closure | P0 | Restore KWS01 model, exact shared preprocessing/postprocessing, representative inputs, labels, independent golden tensors and a real CPU reference executor | All samples match independent goldens; task results pass; 100-repeat stability passes |
| 5. Runner and logging v2 | P0 | Remove synthetic input construction from runner; consume canonical tensors; add correctness/repeatability split, cold/warm timing and schema-v2 evidence fields | JSONL schema validation passes; profile/backend swaps do not change tensor signatures |
| 6. First hardware backend | P0 | Implement KWS01 in the selected Android, HarmonyOS or vendor backend and capture provider/device/partition evidence | Same canonical inputs match KWS01 goldens and strict-NPU mode proves no CPU/GPU fallback |
| 7. Performance scenarios | P1 | Implement cold, warm, sustained and periodic scenarios with deadline misses and stable percentile metrics | Scenario results have unambiguous preparation, cold-first, warm and device timing |
| 8. AVS/DVFS integration | P1 | Add requested/observed operating-point metadata and synchronized power/thermal trace IDs while keeping controls outside the portable runner | Every point records correctness, timing, power, temperature and requested/observed OPP |
| 9. IC01 profile | P1 | Add exact assets, shared semantics, CPU reference and hardware executors | Same gates as KWS01 |
| 10. AD01 profile | P1 | Add exact assets, shared semantics, CPU reference and hardware executors | Same gates as KWS01 |
| 11. SWW01 profile | P2 | Add exact assets, shared semantics, CPU reference and hardware executors | Same gates as KWS01 |
| 12. Regression and release | P2 | Cross-backend conformance, long-duration recovery, deployment docs, asset provenance/licensing and reproducibility bundle | Fresh checkout builds and reproduces documented reference and strict-NPU results |

## 6. Non-negotiable acceptance gates

Before a profile is called implemented:

1. Its manifest, model and sample hashes are fixed and provenance is recorded.
2. All backends report the same ordered tensor names, dtypes, shapes, layouts and
   quantization; the tensor-signature hash is identical.
3. The reference backend matches independent output tensors and task-level
   expectations. A run-local baseline alone does not count.
4. The hardware backend matches the same goldens under documented tolerances.
5. One hundred repeated inferences are stable for deterministic profiles.
6. Unsupported profile/backend pairs fail before allocation with a stable error.
7. Inference timeout plus cleanup returns within timeout plus the documented
   grace period and leaves no pending worker or runtime resource.
8. Global timeout includes initialization, warm-up, inference and teardown.
9. Strict-NPU execution includes provider/device/partition evidence and fails on
   missing evidence or any unexpected CPU/GPU fallback.
10. JSONL validates against its schema and preserves common CPU/GPU field types.

## 7. Immediate next work

Phases 1–3 are now implemented for the repository-owned framework fixture. The
next implementation is KWS01 reference closure: restore the original KWS01
assets with provenance, encode its exact descriptors and shared preprocessing
in a manifest, add independent golden tensors, and implement the real
`reference_cpu/kws01` executor. Runner/logging v2 should then expose manifest,
model and tensor-signature hashes and separate correctness from repeatability
before a hardware backend is integrated.

## 8. Contract-foundation implementation update

The first three immediate contract steps are now implemented:

1. `kws01`, `ic01`, `ad01`, and `sww01` are registered workload profiles and
   backend selection is independent. Until exact manifests exist, unsupported
   pairs fail before allocation with `UNSUPPORTED_PROFILE`.
2. `ProfileSpec`, `TensorSpec`, layout, quantization, checked byte sizing and
   ordered multi-input/multi-output `TensorSet` contracts are active. The
   explicit `framework_smoke` profile exercises two inputs and two outputs.
3. Async execution uses copied work closures, cooperative cancellation and a
   bounded teardown grace period. Per-inference waits are capped by the
   remaining global deadline.

## 9. Manifest and conformance implementation update

The next two phases are now implemented for the framework fixture:

1. A dependency-free UTF-8 JSON parser and versioned manifest schema reject
   duplicate keys, unknown fields, malformed tensor metadata, unsupported
   identities and invalid tolerance policies.
2. Asset paths are canonicalized beneath a declared root. Raw tensor sizes and
   SHA-256 values are verified, and stable manifest and tensor-signature hashes
   are produced.
3. The runner now receives descriptors and input bytes from the manifest rather
   than synthesizing them. The descriptor-driven null backend accepts any
   implemented manifest contract and produces descriptor-correct outputs.
4. Contract tests cover parser behavior, repeatable hashes, invalid fields,
   digest mismatch, path traversal, descriptor mismatch and bounded lifecycle
   behavior. The smoke matrix runs through the checked-in manifest.

The `framework_smoke` assets are only conformance fixtures. The current
`reference_cpu` transform remains a framework test and is not an accuracy
reference. KWS01 reference closure is the next production-profile phase.
