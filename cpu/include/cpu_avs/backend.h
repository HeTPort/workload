#pragma once

#include "cpu_avs/config.h"
#include "cpu_avs/result.h"

#include <cstdint>
#include <memory>
#include <string>

namespace cpu_avs {

struct CpuBatchResult {
    uint64_t operation_count = 0;
    uint64_t checksum = 0;
};

class ICpuBackend {
public:
    virtual ~ICpuBackend() = default;

    virtual bool Init(const WorkloadConfig& cfg, std::string& error) = 0;
    virtual bool CreateResources(std::string& error) = 0;
    virtual BackendStatus RunBatch(uint64_t batch_index, CpuBatchResult& out, std::string& error) = 0;
    virtual uint64_t ExpectedChecksum() const = 0;
    virtual void Destroy() = 0;
    virtual const char* Name() const = 0;
};

std::unique_ptr<ICpuBackend> CreateBackend(const WorkloadConfig& cfg);

} // namespace cpu_avs
