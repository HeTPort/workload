#pragma once

#include "backends/async_backend_base.h"

namespace npu_avs {

class ReferenceCpuBackend final : public AsyncBackendBase {
public:
    bool SupportsProfile(const ProfileSpec& profile) const override;
    BackendStatus Init(const WorkloadConfig& cfg, const ProfileSpec& profile,
                       std::string& error) override;
    BackendStatus CreateResources(std::string& error) override;
    BackendExecutionInfo GetExecutionInfo() const override;
    const char* Name() const override { return "reference_cpu"; }

protected:
    WorkTask MakeTask(const TensorSet& inputs, uint64_t inference_index) const override;
};

} // namespace npu_avs
