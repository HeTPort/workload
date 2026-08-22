#pragma once

#include "gpu_avs/config.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gpu_avs {

bool LoadVulkanSpv(
    const WorkloadConfig& cfg,
    const std::string& filename,
    std::vector<uint32_t>& spv,
    std::string& error
);

} // namespace gpu_avs
