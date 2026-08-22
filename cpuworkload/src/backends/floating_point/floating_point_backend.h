#pragma once

#include "cpu_avs/backend.h"

namespace cpu_avs {

class FloatingPointBackend final : public ICpuBackend {
public:
    bool Init(const WorkloadConfig& cfg, std::string& error) override;
    bool CreateResources(std::string& error) override;
    BackendStatus RunBatch(uint64_t batch_index, CpuBatchResult& out, std::string& error) override;
    uint64_t ExpectedChecksum() const override;
    void Destroy() override;
    const char* Name() const override { return "floating_point"; }

private:
    BackendStatus Execute(CpuBatchResult& out, std::string& error) const;
    WorkloadConfig cfg_;
    uint64_t expected_checksum_ = 0;
};

} // namespace cpu_avs
