#pragma once

#include "cpu_avs/backend.h"
#include "cpu_avs/config.h"

#include <cstdint>
#include <string>

namespace cpu_avs {

struct VerifyResult {
    bool pass = true;
    uint64_t batch = 0;
    std::string verify_mode;
    std::string checksum;
    std::string golden_checksum;
    uint64_t mismatch_count = 0;
    std::string message;
};

class Verifier {
public:
    explicit Verifier(const WorkloadConfig& cfg);
    bool Enabled() const;
    VerifyResult Verify(const CpuBatchResult& batch, uint64_t expected_checksum,
                        uint64_t batch_index) const;
    std::string ComputeChecksum(const CpuBatchResult& batch) const;

private:
    WorkloadConfig cfg_;
};

} // namespace cpu_avs
