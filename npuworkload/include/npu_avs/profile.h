#pragma once

#include "npu_avs/config.h"
#include "npu_avs/tensor.h"

#include <string>
#include <vector>

namespace npu_avs {

struct ProfileSpec {
    std::string id;
    std::string version;
    std::string workload;
    std::string default_backend;
    std::vector<TensorSpec> inputs;
    std::vector<TensorSpec> outputs;
    bool implemented = false;
};

const ProfileSpec* FindProfileSpec(const std::string& profile);
bool ApplyProfileDefaults(const std::string& profile, WorkloadConfig& cfg);
std::vector<std::string> ListProfiles();
void PrintProfiles();

} // namespace npu_avs
