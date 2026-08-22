#include "integer_backend.h"

#include "backends/kernel_common.h"

namespace cpu_avs {
namespace {

uint64_t IntegerKernel(uint64_t seed, uint64_t iterations) {
    uint64_t a = Mix64(seed) | 1ULL;
    uint64_t b = Mix64(seed ^ 0x9e3779b97f4a7c15ULL);
    uint64_t c = seed + 0xd1b54a32d192ed03ULL;
    for (uint64_t i = 0; i < iterations; ++i) {
        a = a * 6364136223846793005ULL + b;
        b ^= RotateLeft64(a, static_cast<unsigned>((i & 31U) + 1U));
        c += (a ^ b) * 0x94d049bb133111ebULL;
        a ^= c >> 17U;
        b += RotateLeft64(c, 23U);
    }
    return Mix64(a ^ RotateLeft64(b, 17U) ^ c);
}

} // namespace

bool IntegerBackend::Init(const WorkloadConfig& cfg, std::string& error) {
    cfg_ = cfg;
    CpuBatchResult reference;
    const BackendStatus status = Execute(reference, error);
    if (status != BackendStatus::Ok) return false;
    expected_checksum_ = reference.checksum;
    return true;
}

bool IntegerBackend::CreateResources(std::string& error) {
    (void)error;
    return true;
}

BackendStatus IntegerBackend::Execute(CpuBatchResult& out, std::string& error) const {
    return RunWorkers(cfg_, SaturatingMultiply(cfg_.iterations, 11U),
        [this](uint32_t worker) {
            return IntegerKernel(cfg_.seed + 0x9e3779b97f4a7c15ULL * worker, cfg_.iterations);
        }, out, error);
}

BackendStatus IntegerBackend::RunBatch(uint64_t batch_index, CpuBatchResult& out, std::string& error) {
    (void)batch_index;
    return Execute(out, error);
}

uint64_t IntegerBackend::ExpectedChecksum() const { return expected_checksum_; }
void IntegerBackend::Destroy() {}

} // namespace cpu_avs
