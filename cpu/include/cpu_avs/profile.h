#pragma once

#include "cpu_avs/config.h"

#include <string>
#include <vector>

namespace cpu_avs {

std::vector<std::string> ListProfiles();
bool ApplyProfileDefaults(const std::string& profile, WorkloadConfig& cfg);
void PrintProfiles();

} // namespace cpu_avs
