#pragma once

#include "gpu_avs/backend.h"

namespace gpu_avs {

class NullBackend final : public IGpuBackend {
public:
    bool Init(const WorkloadConfig& cfg, std::string& error) override;
    bool CreateResources(std::string& error) override;

    SubmitStatus SubmitWorkload(uint64_t frame_index) override;
    bool WaitIdleOrFrameDone(uint64_t timeout_ns, std::string& error) override;

    bool Readback(ReadbackBuffer& out, std::string& error) override;

    bool SupportsGpuTimestamp() const override;
    bool GetLastGpuTimeMs(double& out_ms) override;

    void Destroy() override;

    const char* Name() const override { return "null"; }

private:
    WorkloadConfig cfg_;
    double last_gpu_time_ms_ = 0.0;
};

} // namespace gpu_avs
