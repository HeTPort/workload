#include "npu_avs/verifier.h"

#include "npu_avs/crc32.h"

#include <algorithm>
#include <cctype>

namespace npu_avs {
namespace {

std::string NormalizeHex(std::string value, size_t width) {
    if (value.rfind("0x", 0) == 0 || value.rfind("0X", 0) == 0) value.erase(0, 2);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (value.size() < width) value.insert(value.begin(), width - value.size(), '0');
    return value;
}

} // namespace

Verifier::Verifier(const WorkloadConfig& cfg) : cfg_(cfg) {}

bool Verifier::Enabled() const { return cfg_.verify_mode != "none" || cfg_.generate_golden; }

std::string Verifier::ComputeChecksum(const TensorBuffer& output) const {
    if (cfg_.verify_mode == "crc") return Crc32Hex(output.data.data(), output.data.size());
    return Checksum64Hex(output.data.data(), output.data.size());
}

VerifyResult Verifier::Verify(const TensorBuffer& output, uint64_t inference_index) {
    VerifyResult result;
    result.inference = inference_index;
    result.verify_mode = cfg_.verify_mode;
    result.checksum = ComputeChecksum(output);
    if (cfg_.verify_mode == "none") {
        result.message = "verification disabled";
        return result;
    }

    const size_t width = cfg_.verify_mode == "crc" ? 8U : 16U;
    if (!cfg_.golden_checksum.empty()) {
        result.golden_checksum = NormalizeHex(cfg_.golden_checksum, width);
    } else {
        if (baseline_checksum_.empty()) baseline_checksum_ = result.checksum;
        result.golden_checksum = baseline_checksum_;
    }
    result.pass = result.checksum == result.golden_checksum;
    result.mismatch_count = result.pass ? 0U : 1U;
    result.message = result.pass ? "tensor checksum matched" : "tensor checksum mismatch";
    return result;
}

} // namespace npu_avs
