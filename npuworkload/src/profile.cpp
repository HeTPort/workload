#include "npu_avs/profile.h"

#include <iostream>

namespace npu_avs {

std::vector<std::string> ListProfiles() { return {"null", "reference"}; }

bool ApplyProfileDefaults(const std::string& profile, WorkloadConfig& cfg) {
    cfg = WorkloadConfig{};
    cfg.profile = profile;
    if (profile == "null") {
        cfg.backend = "null";
        cfg.workload = "identity";
        cfg.warmup_inferences = 0;
        return true;
    }
    if (profile == "reference") {
        cfg.backend = "reference_cpu";
        cfg.workload = "synthetic_dense";
        return true;
    }
    return false;
}

void PrintProfiles() {
    for (const std::string& profile : ListProfiles()) std::cout << profile << '\n';
}

} // namespace npu_avs
