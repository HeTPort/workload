#pragma once

#include "npu_avs/config.h"
#include "npu_avs/tensor.h"

#include <cstdint>
#include <string>

namespace npu_avs {

struct VerifyResult {
    bool pass = true;
    uint64_t inference = 0;
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
    VerifyResult Verify(const TensorBuffer& output, uint64_t inference_index);
    std::string ComputeChecksum(const TensorBuffer& output) const;

private:
    WorkloadConfig cfg_;
    std::string baseline_checksum_;
};

} // namespace npu_avs
