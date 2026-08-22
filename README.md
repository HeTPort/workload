# AVS Workloads

This repository contains separate GPU and CPU workload projects for AVS low-power evaluation. The GPU implementation is retained unchanged under `gpuworkload/`; the portable CPU implementation and all of its build, configuration, test, and documentation files are under `cpuworkload/`.

```text
.
├── gpuworkload/
│   ├── configs/       # GPU workload profiles
│   ├── include/       # gpu_avs public/common headers
│   ├── shaders/       # GLES, OpenCL, and Vulkan programs
│   └── src/           # GPU runner and API backends
└── cpuworkload/
    ├── configs/       # CPU workload profiles
    ├── docs/          # CPU design and user manual
    ├── include/       # cpu_avs public/common headers
    ├── src/           # CPU runner and compute backends
    ├── tests/         # CPU smoke tests
    ├── CMakeLists.txt
    ├── CMakePresets.json
    ├── build.ps1
    └── build.sh
```

For CPU build and usage instructions, see [cpuworkload/README.md](cpuworkload/README.md) and the [CPU user manual](cpuworkload/docs/user_manual.md).
