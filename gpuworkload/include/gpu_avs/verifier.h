#pragma once

#include "gpu_avs/backend.h"
#include "gpu_avs/config.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gpu_avs {

struct VerifyResult {
    bool pass = true;
    uint64_t frame = 0;

    std::string verify_mode;
    std::string checksum;
    std::string golden_checksum;

    uint64_t mismatch_count = 0;
    uint64_t pixel_diff_count = 0;
    uint64_t compute_mismatch_count = 0;

    std::string message;
};

class Verifier {
public:
    explicit Verifier(const WorkloadConfig& cfg);

    bool Enabled() const;

    VerifyResult Verify(const ReadbackBuffer& buffer, uint64_t frame_index);

    std::string ComputeChecksum(const ReadbackBuffer& buffer) const;

    bool WriteGoldenFile(
        const ReadbackBuffer& buffer,
        const std::string& path,
        std::string& error
    ) const;

private:
    bool LoadGoldenFile(std::vector<uint8_t>& out, std::string& error) const;

    VerifyResult VerifyChecksumLike(
        const ReadbackBuffer& buffer,
        uint64_t frame_index
    ) const;

    VerifyResult VerifyGoldenFileExact(
        const ReadbackBuffer& buffer,
        uint64_t frame_index,
        bool is_compute
    ) const;

    VerifyResult VerifyPixelDiff(
        const ReadbackBuffer& buffer,
        uint64_t frame_index
    ) const;

private:
    WorkloadConfig cfg_;
};

} // namespace gpu_avs
