#include "mixed_backend.h"

#include "backends/kernel_common.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace cpu_avs {
namespace {

uint64_t MixedKernel(uint64_t seed, uint64_t iterations, uint32_t working_set_kb) {
    const size_t elements = std::max<size_t>(128U,
        static_cast<size_t>(working_set_kb) * 1024U / sizeof(uint64_t));
    std::vector<uint64_t> data(elements);
    uint64_t integer_state = Mix64(seed);
    for (size_t i = 0; i < elements; ++i) {
        data[i] = Mix64(integer_state + i);
    }

    double fp_a = 1.0 + static_cast<double>(seed & 0xffU) / 256.0;
    double fp_b = 0.5;
    uint64_t checksum = seed;
    for (uint64_t i = 0; i < iterations; ++i) {
        integer_state = integer_state * 6364136223846793005ULL + 1442695040888963407ULL;
        const size_t index = static_cast<size_t>((integer_state ^ checksum) % elements);
        const uint64_t value = data[index];
        checksum = RotateLeft64(checksum ^ value, 17U) + integer_state;
        data[index] = Mix64(value + checksum + i);

        fp_a = fp_a * 1.00000011920928955078125 + fp_b * 0.00000095367431640625;
        fp_b = fp_b * 0.999999940395355224609375 + fp_a * 0.000000476837158203125;
        if (fp_a > 16.0) fp_a *= 0.0625;
        if ((integer_state & 7U) == 0U) checksum ^= RotateLeft64(integer_state, 29U);
    }

    uint64_t fp_bits = 0;
    std::memcpy(&fp_bits, &fp_a, sizeof(fp_bits));
    for (size_t i = 0; i < std::min<size_t>(32U, elements); ++i) {
        checksum = Mix64(checksum ^ data[i * elements / std::min<size_t>(32U, elements)]);
    }
    return Mix64(checksum ^ fp_bits);
}

} // namespace

bool MixedBackend::Init(const WorkloadConfig& cfg, std::string& error) {
    cfg_ = cfg;
    CpuBatchResult reference;
    const BackendStatus status = Execute(reference, error);
    if (status != BackendStatus::Ok) return false;
    expected_checksum_ = reference.checksum;
    return true;
}

bool MixedBackend::CreateResources(std::string& error) {
    (void)error;
    return true;
}

BackendStatus MixedBackend::Execute(CpuBatchResult& out, std::string& error) const {
    return RunWorkers(cfg_, SaturatingMultiply(cfg_.iterations, 18U), [this](uint32_t worker) {
        return MixedKernel(cfg_.seed + 0x94d049bb133111ebULL * worker,
                           cfg_.iterations, cfg_.working_set_kb);
    }, out, error);
}

BackendStatus MixedBackend::RunBatch(uint64_t batch_index, CpuBatchResult& out, std::string& error) {
    (void)batch_index;
    return Execute(out, error);
}

uint64_t MixedBackend::ExpectedChecksum() const { return expected_checksum_; }
void MixedBackend::Destroy() {}

} // namespace cpu_avs
