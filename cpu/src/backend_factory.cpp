#include "cpu_avs/backend.h"

#include "backends/floating_point/floating_point_backend.h"
#include "backends/integer/integer_backend.h"
#include "backends/matrix/matrix_backend.h"
#include "backends/memory/memory_backend.h"
#include "backends/mixed/mixed_backend.h"
#include "backends/null/null_backend.h"

#include <memory>

namespace cpu_avs {

std::unique_ptr<ICpuBackend> CreateBackend(const WorkloadConfig& cfg) {
    if (cfg.backend == "null") return std::make_unique<NullBackend>();
    if (cfg.backend == "integer") return std::make_unique<IntegerBackend>();
    if (cfg.backend == "floating_point" || cfg.backend == "fp") {
        return std::make_unique<FloatingPointBackend>();
    }
    if (cfg.backend == "matrix") return std::make_unique<MatrixBackend>();
    if (cfg.backend == "memory") return std::make_unique<MemoryBackend>();
    if (cfg.backend == "mixed") return std::make_unique<MixedBackend>();
    return nullptr;
}

} // namespace cpu_avs
