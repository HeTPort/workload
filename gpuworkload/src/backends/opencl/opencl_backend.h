#pragma once

#include "gpu_avs/backend.h"

#if defined(__APPLE__)
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif

#include <cstdint>
#include <string>
#include <vector>

namespace gpu_avs {

class OpenClBackend final : public IGpuBackend {
public:
    OpenClBackend() = default;
    ~OpenClBackend() override;

    bool Init(const WorkloadConfig& cfg, std::string& error) override;
    bool CreateResources(std::string& error) override;

    SubmitStatus SubmitWorkload(uint64_t frame_index) override;
    bool WaitIdleOrFrameDone(uint64_t timeout_ns, std::string& error) override;

    bool Readback(ReadbackBuffer& out, std::string& error) override;

    bool SupportsGpuTimestamp() const override;
    bool GetLastGpuTimeMs(double& out_ms) override;

    SubmitStatus LastStatus() const override { return last_status_; }

    void Destroy() override;

    const char* Name() const override { return "opencl"; }

private:
    bool PickPlatformAndDevice(std::string& error);
    bool CreateContextAndQueue(std::string& error);
    bool BuildProgramAndKernel(std::string& error);
    bool CreateOutputBuffer(std::string& error);

    bool LoadKernelSource(std::string& source, std::string& error) const;
    std::string BuiltinKernelSource() const;

    uint32_t ShaderId() const;

    void SetStatusFromClError(cl_int err);
    std::string ClErrorToString(cl_int err) const;

    bool IsEventComplete(
        cl_event event,
        cl_int& exec_status,
        std::string& error
    );

private:
    WorkloadConfig cfg_;

    cl_platform_id platform_ = nullptr;
    cl_device_id device_ = nullptr;
    cl_context context_ = nullptr;
    cl_command_queue queue_ = nullptr;

    cl_program program_ = nullptr;
    cl_kernel kernel_ = nullptr;

    cl_mem output_buffer_ = nullptr;
    size_t output_size_ = 0;

    cl_event last_event_ = nullptr;
    bool submitted_ = false;

    std::vector<uint8_t> host_output_;

    bool profiling_supported_ = true;
    double last_gpu_time_ms_ = 0.0;

    bool resources_created_ = false;
    SubmitStatus last_status_ = SubmitStatus::Ok;
};

} // namespace gpu_avs
