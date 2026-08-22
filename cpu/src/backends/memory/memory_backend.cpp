#include "memory_backend.h"

#include "backends/kernel_common.h"

#include <algorithm>
#include <vector>

namespace cpu_avs {
namespace {

uint64_t MemoryKernel(uint64_t seed, uint64_t iterations, uint32_t working_set_kb) {
    const size_t elements = std::max<size_t>(128U,
        static_cast<size_t>(working_set_kb) * 1024U / sizeof(uint64_t));
    std::vector<uint64_t> data(elements);
    uint64_t state = seed;
    for (size_t i = 0; i < elements; ++i) {
        state = Mix64(state + i);
        data[i] = state;
    }

    uint64_t accumulator = seed;
    for (uint64_t i = 0; i < iterations; ++i) {
        state = Mix64(state + i + accumulator);
        const size_t index = static_cast<size_t>(state % elements);
        const uint64_t value = data[index];
        accumulator = RotateLeft64(accumulator ^ value, 13U) + state;
        data[index] = Mix64(value + accumulator + i);
    }

    const size_t samples = std::min<size_t>(64U, elements);
    for (size_t sample = 0; sample < samples; ++sample) {
        const size_t index = sample * elements / samples;
        accumulator = Mix64(accumulator ^ data[index] ^ index);
    }
    return accumulator;
}

} // namespace

bool MemoryBackend::Init(const WorkloadConfig& cfg, std::string& error) {
    cfg_ = cfg;
    CpuBatchResult reference;
    const BackendStatus status = Execute(reference, error);
    if (status != BackendStatus::Ok) return false;
    expected_checksum_ = reference.checksum;
    return true;
}

bool MemoryBackend::CreateResources(std::string& error) {
    (void)error;
    return true;
}

BackendStatus MemoryBackend::Execute(CpuBatchResult& out, std::string& error) const {
    return RunWorkers(cfg_, SaturatingMultiply(cfg_.iterations, 6U), [this](uint32_t worker) {
        return MemoryKernel(cfg_.seed + 0xbf58476d1ce4e5b9ULL * worker,
                            cfg_.iterations, cfg_.working_set_kb);
    }, out, error);
}

BackendStatus MemoryBackend::RunBatch(uint64_t batch_index, CpuBatchResult& out, std::string& error) {
    (void)batch_index;
    return Execute(out, error);
}

uint64_t MemoryBackend::ExpectedChecksum() const { return expected_checksum_; }
void MemoryBackend::Destroy() {}

} // namespace cpu_avs
