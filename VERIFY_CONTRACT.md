# CPU GPU NPU 统一校验协议

当前三个 target 都支持 `--capabilities`、verification contract 2、实际 `verify_count`、`verify_fail_count`、生效 `verify_interval` 和 `success_log_interval`。事件 schema 与 UART protocol 分别管理。

| 含义 | CPU | GPU | NPU |
|---|---|---|---|
| 工作单元 | batch_count | frame_count | inference_count |
| 吞吐 | operations/s | frames/s | inferences/s |
| 预热 | 秒 | 秒 | 次数 |
| 支持的原生验证 | none/checksum/crc | 按能力声明的 checksum/图像等模式 | none/checksum/crc |
| Monitor 正式配置 | none/checksum | none/golden-image | none/checksum/crc，正式资格化要求外部参考 |

verify_interval 选择实际检查点；成功日志采样不会减少检查。success_log_interval 选择首次及每 N 次成功检查，并由一秒间隔限制日志速率；0 关闭成功事件。失败立即报告。正常通过必须能证明真实检查数量与完成工作量相符，失败提前结束时保留真实部分计数。

预热不是正式测量；golden 生成必须从持续性能样本中隔离。NPU 将生成阶段的 inference_count/verify_count 设为零；CPU/GPU 继续通过现有生成流程与校准样本分离，不能把生成摘要当持续测量。无外部 golden 的一致性和完整独立正确性分别表述；null/fake 不代表真实硬件执行。

三类 `include/avs/verify_schedule.h` 使用一致内容，`.gitattributes` 明确 LF，防止 Windows 换行造成字节哈希误差。契约测试和编译产物实际运行测试分别位于 tests/test_verify_contract.ps1、tests/test_runtime_contract.ps1，以及 npuworkload/tests/test_monitor_contract.py。
