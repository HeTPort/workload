#include "cpu_avs/verifier.h"

#include "cpu_avs/crc32.h"
#include "cpu_avs/utils.h"

#include <algorithm>
#include <cctype>

namespace {

std::string NormalizeHex(std::string value, size_t width) {
    if (value.rfind("0x", 0) == 0 || value.rfind("0X", 0) == 0) value.erase(0, 2);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (value.size() < width) value.insert(value.begin(), width - value.size(), '0');
    return value;
}

std::string ChecksumForValue(const cpu_avs::WorkloadConfig& cfg, uint64_t value) {
    if (cfg.verify_mode == "crc") {
        return cpu_avs::Crc32Hex(reinterpret_cast<const uint8_t*>(&value), sizeof(value));
    }
    return cpu_avs::Hex64(value);
}

} // namespace

namespace cpu_avs {

Verifier::Verifier(const WorkloadConfig& cfg)
    : cfg_(cfg) {}

bool Verifier::Enabled() const {
    return cfg_.verify_mode != "none" || cfg_.generate_golden;
}

std::string Verifier::ComputeChecksum(const CpuBatchResult& batch) const {
    return ChecksumForValue(cfg_, batch.checksum);
}

VerifyResult Verifier::Verify(const CpuBatchResult& batch, uint64_t expected_checksum,
                              uint64_t batch_index) const {
    VerifyResult result;
    result.batch = batch_index;
    result.verify_mode = cfg_.verify_mode;
    result.checksum = ComputeChecksum(batch);

    const size_t width = cfg_.verify_mode == "crc" ? 8U : 16U;
    result.golden_checksum = cfg_.golden_checksum.empty()
        ? ChecksumForValue(cfg_, expected_checksum)
        : NormalizeHex(cfg_.golden_checksum, width);

    if (cfg_.verify_mode == "none") {
        result.message = "verification disabled";
        return result;
    }
    if (cfg_.verify_mode != "checksum" && cfg_.verify_mode != "crc") {
        result.pass = false;
        result.mismatch_count = 1;
        result.message = "unsupported CPU verify mode: " + cfg_.verify_mode;
        return result;
    }

    result.pass = result.checksum == result.golden_checksum;
    result.mismatch_count = result.pass ? 0U : 1U;
    result.message = result.pass ? "checksum matched" : "checksum mismatch";
    return result;
}

} // namespace cpu_avs
