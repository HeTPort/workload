# AVS Workloads

This repository contains separate GPU, CPU, and NPU workload projects. The NPU project includes checked KWS01, IC01, AD01, and SWW01 models and inputs, a common ordered-tensor runner, and a generic TensorFlow Lite external-delegate backend for vendor NPU runtimes.

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
    ├── configs/       # Framework smoke configurations
    ├── docs/          # NPU load-generator design
    ├── include/       # npu_avs public/common headers
    ├── profiles/      # Checked models, inputs, and manifests
    ├── src/           # NPU runner and runtime backends
    ├── third_party/   # Model provenance and licenses
    ├── tools/         # Reproducible profile importer
    └── tests/         # NPU smoke tests
```

For CPU build and usage instructions, see [cpuworkload/README.md](cpuworkload/README.md) and the [CPU user manual](cpuworkload/docs/user_manual.md).

For the NPU load generator, see [npuworkload/README.md](npuworkload/README.md) and its [design document](npuworkload/docs/design.md).
