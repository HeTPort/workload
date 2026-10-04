#include "npu_avs/backend.h"

#include "backends/null/null_backend.h"
#include "backends/reference_cpu/reference_cpu_backend.h"
#include "backends/tflite_delegate/tflite_delegate_backend.h"

#include <memory>

namespace npu_avs {

std::unique_ptr<INpuBackend> CreateBackend(const WorkloadConfig& cfg) {
    if (cfg.backend == "null") return std::make_unique<NullBackend>();
    if (cfg.backend == "reference_cpu") return std::make_unique<ReferenceCpuBackend>();
    if (cfg.backend == "tflite_delegate") return std::make_unique<TfliteDelegateBackend>();
    return nullptr;
}

} // namespace npu_avs
