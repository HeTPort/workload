#include "gpu_avs/backend.h"

#include <memory>

#if GPU_AVS_ENABLE_NULL
#include "backends/null/null_backend.h"
#endif

#if GPU_AVS_ENABLE_GLES
#include "backends/gles/gles_backend.h"
#endif

#if GPU_AVS_ENABLE_VULKAN
#include "backends/vulkan/vulkan_backend.h"
#include "backends/vulkan/vulkan_graphics_backend.h"
#endif

#if GPU_AVS_ENABLE_OPENCL
#include "backends/opencl/opencl_backend.h"
#endif

namespace gpu_avs {

std::unique_ptr<IGpuBackend> CreateBackend(const WorkloadConfig& cfg) {
#if GPU_AVS_ENABLE_NULL
    if (cfg.api == "null") {
        return std::make_unique<NullBackend>();
    }
#endif

#if GPU_AVS_ENABLE_GLES
    if (cfg.api == "gles") {
        return std::make_unique<GlesBackend>();
    }
#endif

#if GPU_AVS_ENABLE_VULKAN
    if (cfg.api == "vulkan") {
        if (cfg.mode == "offscreen") {
            return std::make_unique<VulkanGraphicsBackend>();
        }

        return std::make_unique<VulkanBackend>();
    }
#endif

#if GPU_AVS_ENABLE_OPENCL
    if (cfg.api == "opencl") {
        return std::make_unique<OpenClBackend>();
    }
#endif

    return nullptr;
}

} // namespace gpu_avs
