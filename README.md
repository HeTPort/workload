# CPU GPU NPU 压力负载

本仓库统一维护 CPU、GPU、NPU 三类负载。CPU/GPU 的校验协议 v2 与 NPU 集成代码已经在本开发分支合并，原始分支历史保留。统一编译入口为根目录 build.ps1 和 build.sh；详细说明见 docs/zh/编译指南.md。

- CPU：整数、浮点、矩阵、内存、混合及占空比负载。
- GPU：null、GLES、Vulkan、OpenCL，具体可用后端由构建选项和设备驱动决定。
- NPU：KWS01、IC01、AD01、SWW01 四种 INT8 模型，经厂商 TFLite delegate 执行；null/fake runtime 仅用于框架测试。

统一校验约定见 [VERIFY_CONTRACT.md](VERIFY_CONTRACT.md)。集中指南见 [中文交接索引](docs/zh/README.md)。

源码目录：cpuworkload/、gpuworkload/、npuworkload/。各子项目原有构建入口继续保留。GPU 的独立 CMake 为原 standalone OHOS 流程，不替代未包含在本仓库中的其他双框架工程。

主机编译不是 Android/OHOS 交叉编译；未安装对应 NDK 时不会生成板端产物。无电压控制接口时，测试记录属于负载运行和稳定性观察，不作为降压 Vmin 或功耗收益证明。
