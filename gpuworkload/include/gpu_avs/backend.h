#pragma once

#include "gpu_avs/config.h"
#include "gpu_avs/result.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gpu_avs {

struct ReadbackBuffer {
    std::vector<uint8_t> data;
    uint32_t width = 0;
    uint32_t height = 0;
    std::string format;
};

class IGpuBackend {
public:
    virtual ~IGpuBackend() = default;

    virtual bool Init(const WorkloadConfig& cfg, std::string& error) = 0;
    virtual bool CreateResources(std::string& error) = 0;

    virtual SubmitStatus SubmitWorkload(uint64_t frame_index) = 0;
    virtual bool WaitIdleOrFrameDone(uint64_t timeout_ns, std::string& error) = 0;

    virtual bool Readback(ReadbackBuffer& out, std::string& error) = 0;

    virtual bool SupportsGpuTimestamp() const = 0;
    virtual bool GetLastGpuTimeMs(double& out_ms) = 0;

    virtual SubmitStatus LastStatus() const { return SubmitStatus::Ok; }

    virtual void Destroy() = 0;

    virtual const char* Name() const = 0;
};

std::unique_ptr<IGpuBackend> CreateBackend(const WorkloadConfig& cfg);

} // namespace gpu_avs
