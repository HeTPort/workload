#include "npu_avs/verifier.h"

#include "npu_avs/crc32.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

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

void AppendU32(std::vector<uint8_t>& bytes, uint32_t value) {
    for (unsigned shift = 0; shift < 32U; shift += 8U) {
        bytes.push_back(static_cast<uint8_t>((value >> shift) & 0xffU));
    }
}

void AppendString(std::vector<uint8_t>& bytes, const std::string& value) {
    AppendU32(bytes, static_cast<uint32_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}

std::vector<uint8_t> EncodeTensorSet(const TensorSet& outputs) {
    std::vector<uint8_t> encoded;
    AppendU32(encoded, static_cast<uint32_t>(outputs.size()));
    for (const TensorBuffer& output : outputs) {
        AppendString(encoded, output.spec.name);
        AppendString(encoded, TensorDataTypeName(output.spec.data_type));
        AppendString(encoded, TensorLayoutName(output.spec.layout));
        AppendString(encoded, QuantizationModeName(output.spec.quantization.mode));
        AppendU32(encoded, static_cast<uint32_t>(output.spec.shape.size()));
        for (uint32_t dimension : output.spec.shape) AppendU32(encoded, dimension);
        AppendU32(encoded, static_cast<uint32_t>(output.spec.quantization.scales.size()));
        for (float scale : output.spec.quantization.scales) {
            uint32_t bits = 0;
            std::memcpy(&bits, &scale, sizeof(bits));
            AppendU32(encoded, bits);
        }
        for (int32_t zero_point : output.spec.quantization.zero_points) {
            AppendU32(encoded, static_cast<uint32_t>(zero_point));
        }
        AppendU32(encoded, static_cast<uint32_t>(output.spec.quantization.axis));
        AppendU32(encoded, static_cast<uint32_t>(output.data.size()));
        encoded.insert(encoded.end(), output.data.begin(), output.data.end());
    }
    return encoded;
}

} // namespace

Verifier::Verifier(const WorkloadConfig& cfg) : cfg_(cfg) {}

bool Verifier::Enabled() const { return cfg_.verify_mode != "none" || cfg_.generate_golden; }

std::string Verifier::ComputeChecksum(const TensorSet& outputs) const {
    const std::vector<uint8_t> encoded = EncodeTensorSet(outputs);
    if (cfg_.verify_mode == "crc") return Crc32Hex(encoded.data(), encoded.size());
    return Checksum64Hex(encoded.data(), encoded.size());
}

VerifyResult Verifier::Verify(const TensorSet& outputs, uint64_t inference_index) {
    VerifyResult result;
    result.inference = inference_index;
    result.verify_mode = cfg_.verify_mode;
    result.checksum = ComputeChecksum(outputs);
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
    result.message = result.pass ? "tensor-set checksum matched" : "tensor-set checksum mismatch";
    return result;
}

} // namespace npu_avs
