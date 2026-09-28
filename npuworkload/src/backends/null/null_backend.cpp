#include "null_backend.h"

#include "npu_avs/utils.h"

namespace npu_avs {

bool NullBackend::Init(const WorkloadConfig& cfg, std::string& error) {
    (void)error;
    Configure(cfg);
    return true;
}

bool NullBackend::CreateResources(std::string& error) {
    (void)error;
    return true;
}

AsyncBackendBase::WorkResult NullBackend::Execute(const TensorBuffer& input,
                                                   uint64_t inference_index) {
    (void)inference_index;
    if (cfg_.simulated_latency_ms > 0) SleepMs(cfg_.simulated_latency_ms);
    WorkResult work;
    work.inference.output = input;
    work.inference.output.name = "identity_output";
    work.inference.operation_count = TensorElementCount(input);
    return work;
}

BackendExecutionInfo NullBackend::GetExecutionInfo() const {
    return {"null", "none", {"identity:null"}, false};
}

} // namespace npu_avs
