#include "matrix_backend.h"

#include "backends/kernel_common.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace cpu_avs {
namespace {

uint32_t MatrixDimension(uint32_t working_set_kb) {
    const size_t bytes = static_cast<size_t>(working_set_kb) * 1024U;
    const double elements = static_cast<double>(bytes) / (3.0 * sizeof(double));
    const uint32_t dimension = static_cast<uint32_t>(std::sqrt(std::max(64.0, elements)));
    return std::max(8U, std::min(64U, dimension));
}

uint64_t MatrixKernel(uint64_t seed, uint64_t iterations, uint32_t working_set_kb) {
    const uint32_t n = MatrixDimension(working_set_kb);
    const size_t count = static_cast<size_t>(n) * n;
    std::vector<double> a(count);
    std::vector<double> b(count);
    std::vector<double> c(count, 0.0);
    uint64_t state = seed;
    for (size_t i = 0; i < count; ++i) {
        state = Mix64(state + i);
        a[i] = static_cast<double>(state & 0xffffU) / 65536.0;
        state = Mix64(state);
        b[i] = static_cast<double>(state & 0xffffU) / 65536.0;
    }

    const uint64_t repeats = std::max<uint64_t>(1U, iterations / (static_cast<uint64_t>(n) * n));
    for (uint64_t repeat = 0; repeat < repeats; ++repeat) {
        for (uint32_t row = 0; row < n; ++row) {
            for (uint32_t column = 0; column < n; ++column) {
                double sum = c[static_cast<size_t>(row) * n + column] * 0.000001;
                for (uint32_t k = 0; k < n; ++k) {
                    sum += a[static_cast<size_t>(row) * n + k] * b[static_cast<size_t>(k) * n + column];
                }
                c[static_cast<size_t>(row) * n + column] = sum;
            }
        }
    }

    uint64_t checksum = seed;
    for (size_t i = 0; i < count; ++i) {
        uint64_t bits = 0;
        std::memcpy(&bits, &c[i], sizeof(bits));
        checksum = Mix64(checksum ^ bits ^ i);
    }
    return checksum;
}

uint64_t MatrixOperations(const WorkloadConfig& cfg) {
    const uint64_t n = MatrixDimension(cfg.working_set_kb);
    const uint64_t repeats = std::max<uint64_t>(1U, cfg.iterations / (n * n));
    return SaturatingMultiply(repeats, SaturatingMultiply(2U, SaturatingMultiply(n, n * n)));
}

} // namespace

bool MatrixBackend::Init(const WorkloadConfig& cfg, std::string& error) {
    cfg_ = cfg;
    CpuBatchResult reference;
    const BackendStatus status = Execute(reference, error);
    if (status != BackendStatus::Ok) return false;
    expected_checksum_ = reference.checksum;
    return true;
}

bool MatrixBackend::CreateResources(std::string& error) {
    (void)error;
    return true;
}

BackendStatus MatrixBackend::Execute(CpuBatchResult& out, std::string& error) const {
    return RunWorkers(cfg_, MatrixOperations(cfg_), [this](uint32_t worker) {
        return MatrixKernel(cfg_.seed + 0x9e3779b97f4a7c15ULL * worker,
                            cfg_.iterations, cfg_.working_set_kb);
    }, out, error);
}

BackendStatus MatrixBackend::RunBatch(uint64_t batch_index, CpuBatchResult& out, std::string& error) {
    (void)batch_index;
    return Execute(out, error);
}

uint64_t MatrixBackend::ExpectedChecksum() const { return expected_checksum_; }
void MatrixBackend::Destroy() {}

} // namespace cpu_avs
