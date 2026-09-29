#include "npu_avs/profile.h"

#include <iostream>

namespace npu_avs {
namespace {

ProfileSpec FrameworkSmokeProfile() {
    ProfileSpec profile;
    profile.id = "framework_smoke";
    profile.version = "1";
    profile.workload = "framework_smoke";
    profile.default_backend = "reference_cpu";
    profile.inputs = {
        {"features", TensorDataType::Int8, {1U, 65U}, TensorLayout::NC,
         {QuantizationMode::PerTensor, {0.03125F}, {0}, -1}},
        {"context", TensorDataType::UInt8, {1U, 4U}, TensorLayout::NC,
         {QuantizationMode::None, {}, {}, -1}}
    };
    profile.outputs = {
        {"scores", TensorDataType::Int8, {1U, 12U}, TensorLayout::NC,
         {QuantizationMode::PerTensor, {0.03125F}, {0}, -1}},
        {"diagnostics", TensorDataType::Float32, {1U, 2U}, TensorLayout::NC,
         {QuantizationMode::None, {}, {}, -1}}
    };
    profile.implemented = true;
    return profile;
}

const std::vector<ProfileSpec>& Profiles() {
    static const std::vector<ProfileSpec> profiles = {
        {"kws01", "1", "kws01", "reference_cpu", {}, {}, false},
        {"ic01", "1", "ic01", "reference_cpu", {}, {}, false},
        {"ad01", "1", "ad01", "reference_cpu", {}, {}, false},
        {"sww01", "1", "sww01", "reference_cpu", {}, {}, false},
        FrameworkSmokeProfile()
    };
    return profiles;
}

} // namespace

const ProfileSpec* FindProfileSpec(const std::string& profile) {
    for (const ProfileSpec& candidate : Profiles()) {
        if (candidate.id == profile) return &candidate;
    }
    return nullptr;
}

std::vector<std::string> ListProfiles() {
    std::vector<std::string> result;
    for (const ProfileSpec& profile : Profiles()) result.push_back(profile.id);
    return result;
}

bool ApplyProfileDefaults(const std::string& profile, WorkloadConfig& cfg) {
    const ProfileSpec* spec = FindProfileSpec(profile);
    if (!spec) return false;
    cfg = WorkloadConfig{};
    cfg.profile = spec->id;
    cfg.workload = spec->workload;
    cfg.backend = spec->default_backend;
    return true;
}

void PrintProfiles() {
    for (const std::string& profile : ListProfiles()) std::cout << profile << '\n';
}

} // namespace npu_avs
