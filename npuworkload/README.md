# NPU 压力负载

NPU 负载反复调用固定模型，提供运行生命周期、实际校验计数、心跳、耗时和 JSONL 汇总。它不控制电压/频率，不采集功耗/温度，也不证明完整 NPU 算子放置。

| 模型 | 工作 | INT8 输入 | INT8 输出 |
|---|---|---|---|
| kws01 | 关键词识别 | [1,49,10,1] | [1,12] |
| ic01 | 图像分类 | [1,32,32,3] | [1,10] |
| ad01 | 异常检测 | [1,640] | [1,640] |
| sww01 | 唤醒词单窗口 | [1,30,1,40] | [1,3] |

模型和输入来自 MLCommons Tiny，具体文件以资产哈希识别，来源/许可见 third_party/mlcommons-tiny。输入处于模型张量边界，不包含实时采音、图像解码或完整应用链路。

从仓库根目录使用集中编译：

```powershell
.\build.ps1 --components npu --target host --engine direct --test
```

Linux 使用 `sh build.sh --components npu --target host --engine direct --test`。子项目旧 build.ps1/build.sh 继续可用。host 只生成当前主机产物；板端需要对应 Android/OHOS NDK。详细条件见 ../docs/zh/编译指南.md。

null 验证资产和框架，reference_cpu 只用于 framework_smoke 夹具。真实模型用 tflite_delegate，并要求 --runtime-library 与 --delegate-library（和可选 --delegate-options）。动态加载、创建、张量分配或调用失败时停止，不自动去掉 delegate 重试。但 delegate 自身可能仅接管部分算子，完整 NPU 执行须由厂商诊断确认。

`--capabilities` 无需初始化后端，输出 校验能力声明、支持模型/后端和成功日志上限。verify_interval 控制真实校验次数；success_log_interval 控制成功事件采样（0 关闭），并限制最多每秒一条；失败立即输出 pass=false/result=FAIL。预热和 golden 生成不进入正式 inference_count/verify_count。配置使用严格 JSON，拒绝重复键、非法类型和冲突别名。

额外原生协议测试：

```powershell
python tests/test_monitor_contract.py --executable build/desktop-release/npu-avs-workload.exe -v
```

从本目录运行上述命令；集中构建的产物使用对应 build-manifest 记录的路径。null/fake runtime 测试不执行真实模型推理，不能认证 NPU 正确性或物理 AVS 条件。Monitor 侧部署、golden、位置证据与资格化见同级 npu-monitor/docs/NPU_USER_GUIDE.md（中文）。
