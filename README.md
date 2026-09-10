# AVS Workloads

This repository contains separate GPU and CPU workload projects for AVS low-power evaluation. Both projects implement the same verification cadence and success-log contract described in [VERIFY_CONTRACT.md](VERIFY_CONTRACT.md). The portable CPU implementation and its build, configuration, test, and documentation files are under `cpuworkload/`; the GPU implementation is under `gpuworkload/`.

```text
.
├── gpuworkload/
│   ├── configs/       # GPU workload profiles
│   ├── include/       # gpu_avs public/common headers
│   ├── shaders/       # GLES, OpenCL, and Vulkan programs
│   ├── src/           # GPU runner and API backends
│   └── CMakeLists.txt # Standalone HarmonyOS/OHOS framework build
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

`gpuworkload/CMakeLists.txt` formalizes the standalone build previously used by
the HarmonyOS/OHOS framework workflow. The Android/HarmonyOS dual-framework
integration has a separate CMake implementation; this standalone file does not
replace it, and changes must be synchronized and validated there deliberately.

The portable verification-contract tests can be run with:

```powershell
.\tests\test_verify_contract.ps1
```
