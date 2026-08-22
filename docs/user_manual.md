# CPU AVS Workload User Manual

## 1. Overview

`cpu-avs-workload` generates deterministic CPU load for AVS, voltage/frequency, power, thermal, and stability evaluation. It is designed for an external monitoring tool but can also run manually.

The executable writes one JSON object per line. Normal event order is:

```text
start → heartbeat/verify/batch ... → summary
```

Errors produce an `error` record followed by `summary` whenever the process remains able to report.

## 2. Quick start

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
.\build.ps1 -Target desktop -Configuration Release
.\build\desktop-release\cpu-avs-workload.exe --config .\configs\cpu_mixed.json
```

Run a short integer test:

```powershell
.\build\desktop-release\cpu-avs-workload.exe `
  --profile integer --duration 10 --warmup 1 --threads 1 `
  --heartbeat-interval 1
```

Write JSONL to a file:

```powershell
.\build\desktop-release\cpu-avs-workload.exe `
  --config .\configs\cpu_mixed.json --output .\cpu-run.jsonl
```

## 3. Configuration precedence

Effective configuration is resolved in this order:

1. Built-in defaults for the selected profile.
2. Values in `--config <flat-json-file>`.
3. Command-line arguments.

Command-line values therefore override JSON configuration. Inspect the resolved configuration without running:

```sh
cpu-avs-workload --config configs/cpu_mixed.json --threads 4 --dump-effective-config
```

Configuration files must be flat JSON objects. Nested objects and arrays are not part of the interface.

## 4. Profiles

Use `--list-profiles` to list them.

| Profile | Backend | Important defaults | Intended use |
|---|---|---|---|
| `idle` | `null` | 30 s, no warm-up, verification off | Idle/control measurement |
| `integer` | `integer` | 100,000 iterations, 64 KiB | Integer core stress |
| `floating_point` | `floating_point` | 100,000 iterations, 64 KiB | Scalar floating-point stress |
| `matrix` | `matrix` | 20,000 iterations, 96 KiB | Arithmetic/cache stress |
| `memory` | `memory` | 500,000 iterations, 1 MiB | Pseudo-random memory activity |
| `mixed` | `mixed` | 100,000 iterations, 256 KiB | General CPU AVS load |
| `burst` | `mixed` | 50% duty, 2 s period, 120 s | AVS transition response |
| `thermal` | `mixed` | 600 s, 10 s warm-up | Sustained thermal load |
| `stress_extreme` | `mixed` | 500,000 iterations, 2 MiB | Heavy stress |

Except where shown, profiles inherit 60 s duration, 5 s warm-up, 95 s timeout, one thread, checksum verification, and one-second heartbeats.

## 5. Input parameters

### 5.1 Core selection

| CLI | JSON key | Default | Description |
|---|---|---:|---|
| `--profile <name>` | `profile` | `mixed` | Select built-in defaults. |
| `--backend <name>` | `backend` | profile dependent | Select `null`, `integer`, `floating_point`, `matrix`, `memory`, or `mixed`; `fp` aliases `floating_point`. |
| `--kernel <name>` | `kernel` | — | Alias for backend. |
| `--api cpu` | `api` | `cpu` | Compatibility field; only `cpu` is valid. |
| `--mode compute` | `mode` | `compute` | Compatibility field; only `compute` is valid. |
| `--config <path>` | — | empty | Load flat JSON. |
| `--complexity <name>` | `complexity` | profile dependent | Descriptive metadata; actual work is controlled by iterations and footprint. |

### 5.2 Runtime and load

| CLI | JSON key | Default | Description |
|---|---|---:|---|
| `--duration <sec>` | `duration` | profile dependent | Measured duration; may be zero if batches is positive. |
| `--batches <count>` | `batches` | `0` | Batch stop condition; zero disables it. |
| `--frames <count>` | `frames` | — | Legacy alias for batches. |
| `--warmup <sec>` | `warmup` | profile dependent | Warm-up excluded from measured performance. |
| `--timeout <sec>` | `timeout` | profile dependent | Global timeout including initialization/warm-up. |
| `--loop <bool>` | `loop` | `true` | Compatibility field; stop conditions control execution. |
| `--iterations <count>` | `iterations` | profile dependent | Work per worker and batch. |
| `--batch-iterations <count>` | `batch_iterations` | — | Alias for iterations. |
| `--threads <count>` | `threads` | `1` | Worker count from 1 through 256. |
| `--working-set-kb <KiB>` | `working_set_kb` | profile dependent | Approximate per-worker data footprint; aggregate is limited to 1 GiB. |
| `--seed <integer>` | `seed` | `0x123456789abcdef0` | Deterministic seed. CLI accepts decimal or `0x`; JSON may quote hex. |
| `--batch-timeout-ms <ms>` | `batch_timeout_ms` | `5000` | Fail after a returned batch exceeds this duration; zero disables it. |
| `--gpu-timeout-ms <ms>` | `gpu_timeout_ms` | — | Legacy batch-timeout alias. |

If duration and batches are both positive, the first reached condition stops the run.

### 5.3 Duty cycle

| CLI | JSON key | Default | Description |
|---|---|---:|---|
| `--duty-cycle <0..1>` | `duty_cycle` | `1.0` | Values below one enable active/idle burst behavior. |
| `--burst-period <sec>` | `burst_period` | `2.0` | Complete cycle. |
| `--burst-active <sec>` | `burst_active` | `1.0` | Active time at the start of each period. |

Configure `burst_active / burst_period` to match `duty_cycle`. The runtime uses duty cycle to enable burst behavior and active/period for timing.

### 5.4 Verification

| CLI | JSON key | Default | Description |
|---|---|---:|---|
| `--verify-mode <mode>` | `verify_mode` | `checksum` | `none`, `checksum`, or `crc`. |
| `--checksum-interval <n>` | `checksum_interval` | `1` | Emit successful verify events every n batches. Every batch is checked internally; zero suppresses successful events. |
| `--golden-checksum <hex>` | `golden_checksum` | empty | Replace the derived expected result. Checksum accepts 16 hex digits; CRC accepts 8; `0x` is optional. |
| `--fail-fast <bool>` | `fail_fast` | `true` | Stop at first mismatch. |
| `--generate-golden[=<bool>]` | `generate_golden` | `false` | Run one batch, emit golden/summary, and exit. |

Checksum mode emits 16 hex digits. CRC mode calculates CRC32 over the 64-bit kernel result and emits 8.

### 5.5 Monitoring and output

| CLI | JSON key | Default | Description |
|---|---|---:|---|
| `--heartbeat-interval <sec>` | `heartbeat_interval` | `1.0` | Target heartbeat interval; zero disables it. |
| `--output-format jsonl` | `output_format` | `jsonl` | Only JSONL is supported. |
| `--output <path>` | `output` | empty | File output; otherwise stdout. |
| `--summary-only[=<bool>]` | `summary_only` | `false` | Suppress normal events but retain error/summary. |
| `--per-batch-log[=<bool>]` | `per_batch_log` | `false` | Emit every batch. |
| `--per-frame-log[=<bool>]` | `per_frame_log` | — | Legacy per-batch alias. |
| `--log-level <name>` | `log_level` | `info` | Reserved metadata. |

### 5.6 Optional performance gates

| CLI | JSON key | Disabled | Failure condition |
|---|---|---:|---|
| `--min-operations-per-sec <v>` | `min_operations_per_sec` | `0` | Overall throughput is lower. |
| `--max-throughput-cv-pct <v>` | `max_throughput_cv_pct` | `0` | Window-throughput CV is higher. |
| `--max-batch-p99-ms <v>` | `max_batch_p99_ms` | `0` | Batch p99 latency is higher. |
| `--max-heartbeat-gap <sec>` | `max_heartbeat_gap_s` | `0` | Observed heartbeat gap is higher. |
| `--fail-on-instability[=<bool>]` | `fail_on_instability` | `false` | Convert enabled threshold warnings to `PERFORMANCE_FAIL`. |

Do not apply a low CV limit to burst profiles; active/idle windows intentionally create high variation. Calibrate thresholds separately for every backend, device class, thread count, frequency policy, and temperature range.

### 5.7 Utility and legacy flags

| Flag | Behavior |
|---|---|
| `--list-profiles` | Print profiles and exit. |
| `--dump-effective-config` | Print effective JSON and exit. |
| `--help`, `-h` | Print help and exit. |
| `--version` | Print version and exit. |

These GPU-only values are accepted but do not change CPU computation:

```text
width, height, rt-format, samples, texture-count, texture-size,
gpu-timestamp, shader-dir, timestamp-scope, golden-file,
pixel-threshold, pixel-max-diff-count
```

`shader` is different: it aliases the CPU backend.

## 6. Output interface

Every line is one complete JSON object. Consumers should dispatch on `type` and tolerate additive fields in future schema versions.

### 6.1 `start`

Emitted after validation and logger initialization. It contains:

```text
type, schema_version, workload,
profile, api, mode, backend,
duration_s, warmup_s,
iterations, threads, working_set_kb, seed
```

### 6.2 `heartbeat`

The primary live-monitoring record.

| Field | Meaning |
|---|---|
| `timestamp_ms` | Monotonic-clock milliseconds, not Unix wall time |
| `elapsed_s` | Elapsed seconds in current warm-up/measured phase |
| `phase` | `warmup` or `running` |
| `work_unit_count`, `batch_count` | Completed batches in the phase |
| `operation_count` | Completed abstract operations |
| `window_operations_per_sec` | Operations since preceding heartbeat divided by window time |
| `window_batches_per_sec` | Batches per second in that window |
| `last_batch_time_ms` | Most recent batch latency |
| `checksum_mismatch_count` | Mismatches so far |
| `worker_error_count` | Worker/backend errors so far |

Compatibility fields `frame_count`, `gpu_job_count`, `last_frame_time_ms`, and `last_gpu_time_ms` are also emitted. CPU fields are authoritative.

### 6.3 `batch`

Emitted only with `--per-batch-log`.

```text
batch, frame, operation_count,
batch_time_ms, operations_per_sec, checksum
```

### 6.4 `verify`

| Field | Meaning |
|---|---|
| `batch`, `frame` | Verified batch and legacy alias |
| `verify_mode` | `checksum` or `crc` |
| `checksum` | Actual representation |
| `golden_checksum` | Expected representation |
| `result` | `PASS` or `FAIL` |
| `mismatch_count` | Zero or one |
| `compute_mismatch_count` | Compatibility alias |
| `message` | Human-readable detail |

`pixel_diff_count` remains zero for GPU-parser compatibility.

### 6.5 `golden`

Emitted by `--generate-golden`. It contains the profile/API/mode/backend, verification mode, and checksum. Reuse it only with identical backend, seed, threads, iterations, working set, compiler, and floating-point settings.

### 6.6 `error`

```text
timestamp_ms, batch, frame,
error_type, error_code, api_error_code, message
```

`api_error_code` is a compatibility alias for `error_code`.

### 6.7 `summary`

The authoritative final record contains these groups.

Identity and completion:

```text
schema_version, workload, result, exit_code,
profile, api, mode, backend,
threads, iterations_per_batch, working_set_kb,
warmup_s, duration_s,
work_unit_count, batch_count, operation_count
```

Primary performance:

| Field | Meaning |
|---|---|
| `operations_per_sec_avg` | Total operations divided by total measured duration; primary CPU throughput |
| `batches_per_sec_avg` | Batches divided by measured duration |
| `batch_time_ms_*` | Batch-latency distribution |
| `window_operations_per_sec_*` | Heartbeat-window throughput distribution |

Both distribution families contain:

```text
count, avg, min, max, p05, p50, p95, p99, stddev, cv_pct
```

The summary also emits `operations_per_sec_min/max/p05/p50/p95/p99/stddev/cv_pct` as aliases for the window distribution. `operations_per_sec_avg` uniquely means the overall-run average.

Correctness:

```text
verify_pass, verify_mode, verify_fail_count,
first_fail_batch, first_fail_frame,
checksum, golden_checksum, compute_mismatch_count
```

Stability and failures:

```text
performance_stable, performance_warning,
timeout_count, api_error_count, allocation_fail_count,
worker_error_count, heartbeat_max_gap_ms,
heartbeat_last_time_ms, last_error, last_error_code
```

`performance_warning` is a comma-separated combination of:

```text
MIN_OPERATIONS_PER_SEC
THROUGHPUT_CV_EXCEEDED
BATCH_P99_EXCEEDED
HEARTBEAT_GAP_EXCEEDED
```

Legacy GPU summary fields remain present. Frame fields alias batch fields; GPU timestamp/job-time fields are false or zero.

## 7. Exit codes and monitoring policy

| Exit | Result | Meaning |
|---:|---|---|
| 0 | `PASS` | Completed without enabled failure conditions |
| 1 | `CHECKSUM_FAIL` | Deterministic CPU result mismatch |
| 2 | `API_ERROR` | Backend selection/init/execution failure; name retained for compatibility |
| 3 | `TIMEOUT` | Global or batch timeout |
| 4 | `DEVICE_LOST` | Reserved GPU-compatible value; CPU runner does not currently produce it |
| 5 | `ALLOCATION_FAIL` | Backend allocation failure |
| 6 | `UNKNOWN_ERROR` | Invalid configuration or uncategorized failure |
| 7 | `PERFORMANCE_FAIL` | Enabled threshold violated with performance gating active |

Recommended monitoring rules:

- Always fail on nonzero exit, `verify_pass=false`, mismatch count, timeout, worker error, or missing final summary.
- Treat a missing heartbeat beyond the monitor's independent deadline as a hang/crash.
- Treat `performance_stable=false` as a warning unless performance gating was explicitly enabled.
- Establish performance thresholds under controlled frequency, affinity, temperature, background-load, and thread-count conditions.

## 8. Building

### 8.1 Windows desktop

```powershell
.\build.ps1 -Target desktop -Configuration Release
```

If CMake is unavailable but `g++` is on `PATH`, the script invokes the complete source list directly. Clean before building with `-Clean`.

Expected output:

```text
build/desktop-release/cpu-avs-workload.exe
```

### 8.2 Android arm64

```powershell
$env:ANDROID_NDK_HOME = "D:\Android\Sdk\ndk\<version>"
.\build.ps1 -Target android-arm64 -Configuration Release
```

Manual CMake equivalent:

```sh
cmake -S . -B build/android-arm64-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-24 \
  -DANDROID_STL=c++_static
cmake --build build/android-arm64-release --parallel
```

### 8.3 HarmonyOS arm64

```powershell
$env:OHOS_NDK_HOME = "D:\Huawei\Sdk\<version>\openharmony\native"
.\build.ps1 -Target harmony-arm64 -Configuration Release
```

Manual CMake equivalent:

```sh
cmake -S . -B build/harmony-arm64-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$OHOS_NDK_HOME/build/cmake/ohos.toolchain.cmake" \
  -DOHOS_ARCH=arm64-v8a \
  -DOHOS_STL=c++_static
cmake --build build/harmony-arm64-release --parallel
```

SDK layouts vary. `OHOS_NDK_HOME` must contain `build/cmake/ohos.toolchain.cmake`.

### 8.4 POSIX helper and CMake presets

```sh
sh ./build.sh desktop Release
sh ./build.sh android-arm64 Release
sh ./build.sh harmony-arm64 Release
```

With Ninja and the necessary environment variable:

```sh
cmake --preset desktop-release
cmake --build --preset desktop-release
cmake --preset android-arm64-release
cmake --build --preset android-arm64-release
cmake --preset harmony-arm64-release
cmake --build --preset harmony-arm64-release
```

### 8.5 Static runtime audit

Android and HarmonyOS configurations select `c++_static`, avoiding a separately shipped private `libc++_shared.so`. The executable may still use platform system libraries and its native loader.

Audit each target artifact with its NDK tool:

```sh
llvm-readelf -d cpu-avs-workload | grep NEEDED
```

Confirm that unwanted private dependencies such as `libc++_shared.so` are absent.

## 9. Tests

PowerShell smoke suite:

```powershell
.\tests\smoke.ps1 `
  -Executable .\build\desktop-release\cpu-avs-workload.exe
```

It checks integer, floating-point, matrix, memory, two-thread mixed, deliberate checksum failure, and deliberate performance failure.

CMake/CTest:

```sh
ctest --test-dir build/desktop-release --output-on-failure
```

## 10. Monitoring examples

Correctness/liveness run without performance gating:

```sh
cpu-avs-workload \
  --profile mixed --duration 60 --warmup 5 --threads 4 \
  --heartbeat-interval 1 --checksum-interval 60
```

Controlled stability run:

```sh
cpu-avs-workload \
  --profile integer --duration 60 --warmup 5 --threads 1 \
  --min-operations-per-sec 500000000 \
  --max-throughput-cv-pct 5 \
  --max-batch-p99-ms 5 \
  --max-heartbeat-gap 2.5 \
  --fail-on-instability
```

These thresholds are examples only. Calibrate the actual target device and test conditions before using performance gating.
