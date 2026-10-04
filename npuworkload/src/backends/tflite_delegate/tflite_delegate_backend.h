#pragma once

#include "backends/async_backend_base.h"

#include <memory>

namespace npu_avs {

class TfliteDelegateBackend final : public AsyncBackendBase {
public:
    struct RuntimeState;

    TfliteDelegateBackend();
    ~TfliteDelegateBackend() override;

    bool SupportsProfile(const ProfileSpec& profile) const override;
    BackendStatus Init(const WorkloadConfig& cfg, const ProfileSpec& profile,
                       std::string& error) override;
    BackendStatus CreateResources(std::string& error) override;
    BackendExecutionInfo GetExecutionInfo() const override;
    BackendStatus Destroy(uint32_t timeout_ms, std::string& error) override;
    const char* Name() const override { return "tflite_delegate"; }

protected:
    WorkTask MakeTask(const TensorSet& inputs, uint64_t inference_index) const override;

private:
    std::shared_ptr<RuntimeState> runtime_;
};

} // namespace npu_avs
