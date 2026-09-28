#pragma once

#include "backends/async_backend_base.h"

namespace npu_avs {

class ReferenceCpuBackend final : public AsyncBackendBase {
public:
    bool Init(const WorkloadConfig& cfg, std::string& error) override;
    bool CreateResources(std::string& error) override;
    BackendExecutionInfo GetExecutionInfo() const override;
    const char* Name() const override { return "reference_cpu"; }

protected:
    WorkResult Execute(const TensorBuffer& input, uint64_t inference_index) override;
};

} // namespace npu_avs
