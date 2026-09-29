#pragma once

#include "npu_avs/profile.h"

#include <string>

namespace npu_avs {

struct ModelAsset {
    std::string format;
    std::string path;
    std::string sha256;
    std::string provenance;
};

struct TolerancePolicy {
    std::string mode = "exact";
    double absolute = 0.0;
    double relative = 0.0;
};

struct ProfileManifest {
    int schema_version = 0;
    ProfileSpec profile;
    std::string manifest_path;
    std::string asset_root;
    std::string manifest_sha256;
    std::string tensor_signature_sha256;
    ModelAsset model;
    std::string sample_id;
    std::string preprocessing_version;
    std::string postprocessing_version;
    TensorSet inputs;
    TensorSet goldens;
    TolerancePolicy tolerance;
};

bool LoadProfileManifest(const std::string& path, const std::string& expected_profile,
                         ProfileManifest& manifest, std::string& error);
std::string ComputeTensorSignatureSha256(const ProfileSpec& profile);

} // namespace npu_avs
