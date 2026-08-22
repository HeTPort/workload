#include "null_backend.h"

#include "cpu_avs/utils.h"
#include "backends/kernel_common.h"

namespace cpu_avs {

bool NullBackend::Init(const WorkloadConfig& cfg, std::string& error) {
    (void)error;
    cfg_ = cfg;
    CpuBatchResult reference;
    const BackendStatus status = RunWorkers(cfg_, 0U, [this](uint32_t worker) {
        return Mix64(cfg_.seed + worker);
    }, reference, error);
    if (status != BackendStatus::Ok) return false;
    expected_checksum_ = reference.checksum;
    return true;
}

bool NullBackend::CreateResources(std::string& error) {
    (void)error;
    return true;
}

BackendStatus NullBackend::RunBatch(uint64_t batch_index, CpuBatchResult& out, std::string& error) {
    (void)batch_index;
    SleepMs(1U);
    return RunWorkers(cfg_, 0U, [this](uint32_t worker) {
        return Mix64(cfg_.seed + worker);
    }, out, error);
}

uint64_t NullBackend::ExpectedChecksum() const { return expected_checksum_; }
void NullBackend::Destroy() {}

} // namespace cpu_avs
