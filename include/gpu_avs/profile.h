#pragma once

#include "gpu_avs/config.h"

#include <string>
#include <vector>

namespace gpu_avs {

std::vector<std::string> ListProfiles();

bool ApplyProfileDefaults(const std::string& profile, WorkloadConfig& cfg);

void PrintProfiles();

} // namespace gpu_avs
