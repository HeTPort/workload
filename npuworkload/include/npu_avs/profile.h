#pragma once

#include "npu_avs/config.h"

#include <string>
#include <vector>

namespace npu_avs {

bool ApplyProfileDefaults(const std::string& profile, WorkloadConfig& cfg);
std::vector<std::string> ListProfiles();
void PrintProfiles();

} // namespace npu_avs
