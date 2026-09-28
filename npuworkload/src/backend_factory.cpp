#include "npu_avs/backend.h"

#include "backends/null/null_backend.h"
#include "backends/reference_cpu/reference_cpu_backend.h"

#include <memory>

namespace npu_avs {

std::unique_ptr<INpuBackend> CreateBackend(const WorkloadConfig& cfg) {
    if (cfg.backend == "null") return std::make_unique<NullBackend>();
    if (cfg.backend == "reference_cpu") return std::make_unique<ReferenceCpuBackend>();
    return nullptr;
}

} // namespace npu_avs
