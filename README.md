# AVS Workloads

This repository contains separate GPU, CPU, and NPU workload projects for AVS low-power evaluation. The NPU project currently provides its portable runner, workload-profile registry, ordered multi-tensor asynchronous backend contract, bounded timeout cleanup, and deterministic framework-test backends; real workload assets and hardware runtime integration follow in later phases.

```text
.
├── gpuworkload/
│   ├── configs/       # GPU workload profiles
│   ├── include/       # gpu_avs public/common headers
│   ├── shaders/       # GLES, OpenCL, and Vulkan programs
│   └── src/           # GPU runner and API backends
├── cpuworkload/
│   ├── configs/       # CPU workload profiles
│   ├── docs/          # CPU design and user manual
│   ├── include/       # cpu_avs public/common headers
│   ├── src/           # CPU runner and compute backends
│   ├── tests/         # CPU smoke tests
│   ├── CMakeLists.txt
│   ├── CMakePresets.json
│   ├── build.ps1
│   └── build.sh
└── npuworkload/
    ├── configs/       # NPU baseline profiles
    ├── docs/          # NPU baseline design
    ├── include/       # npu_avs public/common headers
    ├── src/           # NPU runner and initial backends
    └── tests/         # NPU smoke tests
```

For CPU build and usage instructions, see [cpuworkload/README.md](cpuworkload/README.md) and the [CPU user manual](cpuworkload/docs/user_manual.md).

For the NPU baseline, see [npuworkload/README.md](npuworkload/README.md) and its [design document](npuworkload/docs/design.md).
