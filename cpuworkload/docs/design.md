# CPU AVS Workload Design

## 1. Purpose

`cpu-avs-workload` is a portable, deterministic CPU loading and health-check tool for AVS low-power evaluation. It follows the existing GPU workload's factory/backend organization and preserves the monitoring-facing JSONL lifecycle while replacing GPU API work with CPU computation batches.

The CPU tool is additive. The original GPU source, shaders, and configurations remain untouched in the sibling `gpuworkload/` directory.

## 2. Design objectives

- Build the same C++17 sources for desktop, Android, and HarmonyOS.
- Keep platform selection in CMake/toolchain configuration.
- Avoid GLES, Vulkan, OpenCL, JNI, N-API, and other platform runtime dependencies.
- Preserve configuration precedence: profile defaults, JSON config, CLI overrides.
- Preserve start, heartbeat, verify, golden, error, and summary event semantics.
- Make correctness failure independent of performance variability.
- Report windowed throughput and latency distributions suitable for an external monitor.
- Keep existing exit-code values 0–6 stable and add performance failure as exit 7.

## 3. Project structure

```text
cpuworkload/
├── CMakeLists.txt
├── CMakePresets.json
├── build.ps1
├── build.sh
├── configs/
├── docs/
├── include/cpu_avs/
│   ├── backend.h
│   ├── config.h
│   ├── crc32.h
│   ├── heartbeat.h
│   ├── logger.h
│   ├── metrics.h
│   ├── profile.h
│   ├── result.h
│   ├── runner.h
│   ├── utils.h
│   └── verifier.h
├── src/
│   ├── backend_factory.cpp
│   ├── config.cpp
│   ├── crc32.cpp
│   ├── heartbeat.cpp
│   ├── logger.cpp
│   ├── main.cpp
│   ├── metrics.cpp
│   ├── profile.cpp
│   ├── runner.cpp
│   ├── utils.cpp
│   ├── verifier.cpp
│   └── backends/
│       ├── kernel_common.h
│       ├── null/
│       ├── integer/
│       ├── floating_point/
│       ├── matrix/
│       ├── memory/
│       └── mixed/
└── tests/
```

The common-file inventory mirrors the GPU implementation. CPU backends replace the GPU API backends; shader assets, graphics contexts, and shader loaders have no CPU counterpart.

## 4. Backend contract

`ICpuBackend` owns initialization, resource preparation, one synchronous computation batch, expected-result calculation, and cleanup.

```cpp
struct CpuBatchResult {
    uint64_t operation_count;
    uint64_t checksum;
};

class ICpuBackend {
public:
    virtual bool Init(const WorkloadConfig&, std::string& error) = 0;
    virtual bool CreateResources(std::string& error) = 0;
    virtual BackendStatus RunBatch(uint64_t index, CpuBatchResult&, std::string& error) = 0;
    virtual uint64_t ExpectedChecksum() const = 0;
    virtual void Destroy() = 0;
    virtual const char* Name() const = 0;
};
```

The factory selects the backend from `--backend`. `--shader` is retained as a compatibility alias for this selection.

## 5. Backend behavior

| Backend | Principal stress | Configurable footprint |
|---|---|---|
| `null` | Idle/control timing | No |
| `integer` | Dependent integer multiply, add, XOR, and rotate chains | Iterations and threads |
| `floating_point` | Dependent scalar floating-point multiply/add and branches | Iterations and threads |
| `matrix` | Dense matrix arithmetic with cache-resident data | Iterations, working set, threads |
| `memory` | Deterministic pseudo-random read/modify/write access | Iterations, working set, threads |
| `mixed` | Integer, floating-point, branch, and memory activity | Iterations, working set, threads |

Each worker receives a deterministic seed derived from the runtime seed and worker index. Worker results are combined in worker-index order, so thread completion order cannot change the checksum.

`operation_count` is an abstract, deterministic work-unit count assigned by each backend. It is suitable for within-backend throughput comparison. It is not a hardware retired-instruction count and must not be compared across different backend types as though it were one.

## 6. Execution lifecycle

```text
parse CLI
→ apply profile defaults
→ apply JSON config
→ apply CLI overrides
→ validate configuration
→ emit start
→ create/init backend and untimed expected checksum
→ warm up
→ run active/idle duty-cycle loop
   → execute batch
   → check batch timeout
   → verify deterministic checksum
   → collect batch latency
   → emit optional batch/verify records
   → emit heartbeat and window throughput
→ evaluate optional performance thresholds
→ emit summary
→ return stable exit code
```

Duration and batch-count stop conditions use first-condition-wins semantics when both are positive. The global timeout includes initialization and warm-up. Batch timeout can only be evaluated after a synchronous batch returns; an external monitor should use missing heartbeats to detect a hard-hung batch or process.

## 7. Correctness model

During backend initialization, the configured computation is run once outside measured time to establish the expected checksum. Every measured batch is compared against it even if verify-event emission is infrequent.

- `verify_mode=checksum`: compare the 64-bit deterministic result, rendered as 16 hex digits.
- `verify_mode=crc`: CRC32 the 64-bit deterministic result and compare 8 hex digits.
- `verify_mode=none`: record work but do not gate on correctness.
- `golden_checksum`: optionally replaces the initialization-derived expected representation.

A mismatch increments `verify_fail_count`, records `first_fail_batch`, and produces `CHECKSUM_FAIL` unless a higher-priority execution failure already ended the run.

## 8. Metrics and stability

Batch latency samples produce average, min, max, p05, p50, p95, p99, population standard deviation, and coefficient of variation. Heartbeat windows produce the same distribution for operations per second.

Correctness/liveness failures are hard failures. Performance thresholds are optional because scheduler activity, DVFS, temperature, and intentional burst duty cycles can legitimately vary throughput.

```text
performance_stable =
    operations_per_sec_avg >= configured minimum
    and throughput_cv_pct <= configured maximum
    and batch_time_p99_ms <= configured maximum
    and heartbeat_max_gap_ms <= configured maximum
```

Threshold violations become exit 7 only with `--fail-on-instability`; otherwise they are reported as warnings in a PASS summary.

## 9. Portability boundary

Runtime code uses standard C++17 only: chrono, threads, containers, integer/floating-point arithmetic, streams, and flat file I/O. CPU affinity, scheduling policy, topology, frequency, power, and temperature are intentionally outside the runtime boundary and may be controlled/observed by the external monitor.

Android and HarmonyOS still require their target compiler and sysroot. Both NDK build paths select static libc++, which removes a private `libc++_shared.so` deployment dependency. This does not imply a fully static operating-system-independent ELF; device system libraries and the platform loader remain valid runtime dependencies.

## 10. Compatibility decisions

- `--frames` aliases `--batches`.
- `--per-frame-log` aliases `--per-batch-log`.
- `--gpu-timeout-ms` aliases `--batch-timeout-ms`.
- `--shader` aliases `--backend`.
- GPU-only resolution, texture, timestamp, image-golden, and pixel-diff inputs are accepted where listed in the user manual but do not influence CPU computation.
- JSON heartbeat/summary records retain selected `frame_*`, `fps_*`, `gpu_*`, and error-field aliases for shared parsers. CPU-native fields are authoritative.

## 11. Development reviews

1. Architecture review confirmed that all GPU common framework components were retained and only GPU-specific backends/assets were replaced.
2. Backend review confirmed deterministic validation, runtime-observable results, multi-thread checksum ordering, allocation bounds, and portable C++ implementation.
3. Runtime review confirmed event ordering, warm-up and burst behavior, timeout/error paths, JSON validity, exit codes, and performance gating.
