# NPU Profile Manifest Contract

The profile manifest is strict JSON encoded as UTF-8. Schema version 1 has the following required top-level fields and rejects all others:

- `schema_version`: integer `1`.
- `profile`: `id`, `version`, `workload`, and `default_backend`; all must match the selected registered profile.
- `asset_root`: non-empty relative directory below the manifest directory.
- `model`: `format`, `file`, `sha256`, and non-empty `provenance`. The test-only format `none` requires empty file and hash values.
- `sample_id`, `preprocessing_version`, and `postprocessing_version`: non-empty strings.
- `inputs`: non-empty ordered array of tensor descriptors with `file` and `sha256`.
- `outputs`: non-empty ordered array of tensor descriptors.
- `goldens`: ordered output-tensor assets, or an empty array when no independent goldens exist.
- `tolerance`: `mode`, `absolute`, and `relative`. Version 1 accepts `exact` or `absolute_relative`.

Each tensor descriptor requires `name`, `dtype`, `shape`, `layout`, and `quantization`. Input and golden descriptors additionally require `file` and a 64-character SHA-256. Stable dtype names are `int8`, `uint8`, `int16`, `int32`, `float16`, and `float32`; stable layouts are `scalar`, `nc`, `nchw`, `nhwc`, `ntc`, and `raw`. Quantization modes are `none`, `per_tensor`, and `per_axis` and follow the public tensor contract documented in the development plan.

Files are resolved canonically under `asset_root`. Absolute paths, empty paths, traversal outside that root, missing files, descriptor-size mismatches, and digest mismatches fail before backend initialization. Raw tensor files contain only contiguous tensor bytes in the declared layout; multi-byte elements are little-endian.

Example invocation:

```powershell
./build/desktop-release/npu-avs-workload.exe `
  --profile framework_smoke `
  --backend null `
  --input-manifest ./profiles/framework_smoke/manifest.json `
  --duration 0 --inferences 2 --warmup-inferences 0
```

The loader exposes both the digest of the exact manifest file and a canonical tensor-signature digest. The signature covers profile identity plus ordered input/output names, dtypes, layouts, shapes, and quantization. It must match the approved contract in the profile registry, so a caller-supplied manifest cannot activate an unfinished profile or redefine a backend's interface.
