# CPU GPU NPU 中文资料

三类负载已统一在当前源码树；集中入口为根目录 build.ps1/build.sh。

- [编译指南](编译指南.md)
- [设计指南](设计指南.md)
- [Monitor 交接资料](../../../npu-monitor/docs/zh/README.md)（按同级 npu-monitor/npu-workload 布局）
- [用例原理与完整配置表](../../../npu-monitor/docs/zh/用例总表.md)
- [七天压测和日志要求](../../../npu-monitor/docs/zh/七天压测与日志交接.md)

CPU/GPU 校验分支来自 GitHub 5d6aca9，NPU 基线来自 ccf431f，NPU 协议增强为 8551028。交接时以实际 git 提交和 build-manifest.json 为准。原有英文文档作为历史实现资料保留，中文交接手册为本次操作入口。
