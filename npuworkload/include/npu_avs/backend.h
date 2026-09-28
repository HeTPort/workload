#pragma once

#include "npu_avs/config.h"
#include "npu_avs/result.h"
#include "npu_avs/tensor.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace npu_avs {

struct BackendExecutionInfo {
    std::string runtime;
    std::string execution_target;
    std::vector<std::string> partitions;
    bool fallback_used = false;
};

struct InferenceResult {
    TensorBuffer output;
    uint64_t operation_count = 0;
    double device_time_ms = 0.0;
    bool device_time_valid = false;
};

class INpuBackend {
public:
    virtual ~INpuBackend() = default;

    virtual bool Init(const WorkloadConfig& cfg, std::string& error) = 0;
    virtual bool CreateResources(std::string& error) = 0;
    virtual bool SetInput(const TensorBuffer& input, std::string& error) = 0;
    virtual BackendStatus SubmitInference(uint64_t inference_index, std::string& error) = 0;
    virtual BackendStatus WaitForCompletion(uint32_t timeout_ms, std::string& error) = 0;
    virtual bool ReadOutput(InferenceResult& output, std::string& error) = 0;
    virtual BackendExecutionInfo GetExecutionInfo() const = 0;
    virtual void Destroy() = 0;
    virtual const char* Name() const = 0;
};

std::unique_ptr<INpuBackend> CreateBackend(const WorkloadConfig& cfg);

} // namespace npu_avs
