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

ProfileSpec KwsProfile() {
    ProfileSpec profile;
    profile.id = "kws01";
    profile.version = "1";
    profile.workload = "kws01";
    profile.default_backend = "tflite_delegate";
    profile.model_format = "tflite";
    profile.inputs = {
        {"input_1", TensorDataType::Int8, {1U, 49U, 10U, 1U}, TensorLayout::NHWC,
         {QuantizationMode::PerTensor, {0.5847029089927673F}, {83}, -1}}
    };
    profile.outputs = {
        {"Identity", TensorDataType::Int8, {1U, 12U}, TensorLayout::NC,
         {QuantizationMode::PerTensor, {0.00390625F}, {-128}, -1}}
    };
    profile.implemented = true;
    return profile;
}

ProfileSpec IcProfile() {
    ProfileSpec profile;
    profile.id = "ic01";
    profile.version = "1";
    profile.workload = "ic01";
    profile.default_backend = "tflite_delegate";
    profile.model_format = "tflite";
    profile.inputs = {
        {"input_1_int8", TensorDataType::Int8, {1U, 32U, 32U, 3U}, TensorLayout::NHWC,
         {QuantizationMode::PerTensor, {1.0F}, {-128}, -1}}
    };
    profile.outputs = {
        {"Identity_int8", TensorDataType::Int8, {1U, 10U}, TensorLayout::NC,
         {QuantizationMode::PerTensor, {0.00390625F}, {-128}, -1}}
    };
    profile.implemented = true;
    return profile;
}

ProfileSpec AdProfile() {
    ProfileSpec profile;
    profile.id = "ad01";
    profile.version = "1";
    profile.workload = "ad01";
    profile.default_backend = "tflite_delegate";
    profile.model_format = "tflite";
    profile.inputs = {
        {"input_1", TensorDataType::Int8, {1U, 640U}, TensorLayout::NC,
         {QuantizationMode::PerTensor, {0.3910152316093445F}, {89}, -1}}
    };
    profile.outputs = {
        {"Identity", TensorDataType::Int8, {1U, 640U}, TensorLayout::NC,
         {QuantizationMode::PerTensor, {0.36449846625328064F}, {96}, -1}}
    };
    profile.implemented = true;
    return profile;
}

ProfileSpec SwwProfile() {
    ProfileSpec profile;
    profile.id = "sww01";
    profile.version = "1";
    profile.workload = "sww01";
    profile.default_backend = "tflite_delegate";
    profile.model_format = "tflite";
    profile.inputs = {
        {"serving_default_input_1:0", TensorDataType::Int8, {1U, 30U, 1U, 40U},
         TensorLayout::NHWC,
         {QuantizationMode::PerTensor, {0.003701042616739869F}, {-128}, -1}}
    };
    profile.outputs = {
        {"StatefulPartitionedCall:0", TensorDataType::Int8, {1U, 3U}, TensorLayout::NC,
         {QuantizationMode::PerTensor, {0.00390625F}, {-128}, -1}}
    };
    profile.implemented = true;
    return profile;
}

const std::vector<ProfileSpec>& Profiles() {
    static const std::vector<ProfileSpec> profiles = {
        KwsProfile(),
        IcProfile(),
        AdProfile(),
        SwwProfile(),
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
