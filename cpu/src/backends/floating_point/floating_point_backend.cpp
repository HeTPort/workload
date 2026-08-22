#include "floating_point_backend.h"

#include "backends/kernel_common.h"

#include <cstring>

namespace cpu_avs {
namespace {

uint64_t FloatingPointKernel(uint64_t seed, uint64_t iterations) {
    double a = 1.0 + static_cast<double>(seed & 0xffffU) / 65536.0;
    double b = 0.5 + static_cast<double>((seed >> 16U) & 0xffffU) / 131072.0;
    double c = 0.25 + static_cast<double>((seed >> 32U) & 0xffffU) / 262144.0;
    for (uint64_t i = 0; i < iterations; ++i) {
        a = a * 1.00000011920928955078125 + b * 0.00000095367431640625;
        b = b * 0.999999940395355224609375 + c * 0.0000019073486328125;
        c = c + (a - b) * 0.000000476837158203125;
        if (a > 16.0) a *= 0.0625;
        if (b < 0.125) b += 0.75;
        if (c > 8.0) c -= 4.0;
    }
    uint64_t a_bits = 0;
    uint64_t b_bits = 0;
    uint64_t c_bits = 0;
    std::memcpy(&a_bits, &a, sizeof(a_bits));
    std::memcpy(&b_bits, &b, sizeof(b_bits));
    std::memcpy(&c_bits, &c, sizeof(c_bits));
    return Mix64(a_bits ^ RotateLeft64(b_bits, 19U) ^ RotateLeft64(c_bits, 41U));
}

} // namespace

bool FloatingPointBackend::Init(const WorkloadConfig& cfg, std::string& error) {
    cfg_ = cfg;
    CpuBatchResult reference;
    const BackendStatus status = Execute(reference, error);
    if (status != BackendStatus::Ok) return false;
    expected_checksum_ = reference.checksum;
    return true;
}

bool FloatingPointBackend::CreateResources(std::string& error) {
    (void)error;
    return true;
}

BackendStatus FloatingPointBackend::Execute(CpuBatchResult& out, std::string& error) const {
    return RunWorkers(cfg_, SaturatingMultiply(cfg_.iterations, 12U),
        [this](uint32_t worker) {
            return FloatingPointKernel(cfg_.seed + 0xd1b54a32d192ed03ULL * worker, cfg_.iterations);
        }, out, error);
}

BackendStatus FloatingPointBackend::RunBatch(uint64_t batch_index, CpuBatchResult& out,
                                             std::string& error) {
    (void)batch_index;
    return Execute(out, error);
}

uint64_t FloatingPointBackend::ExpectedChecksum() const { return expected_checksum_; }
void FloatingPointBackend::Destroy() {}

} // namespace cpu_avs
