#include "backends/null/null_backend.h"
#include "npu_avs/json.h"
#include "npu_avs/manifest.h"
#include "npu_avs/profile.h"
#include "npu_avs/sha256.h"
#include "npu_avs/tensor.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    Require(stream.good(), "failed to open manifest fixture");
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    Require(stream.good() || stream.eof(), "failed to read manifest fixture");
    return buffer.str();
}

void ReplaceOnce(std::string& text, const std::string& before, const std::string& after) {
    const size_t position = text.find(before);
    Require(position != std::string::npos, "manifest mutation target was not found");
    text.replace(position, before.size(), after);
}

class TemporaryFixture {
public:
    explicit TemporaryFixture(const std::filesystem::path& source_manifest) {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
                ("npu-contract-" + std::to_string(suffix));
        std::filesystem::create_directories(root_ / "assets");
        const std::filesystem::path source_assets = source_manifest.parent_path() / "assets";
        std::filesystem::copy_file(source_assets / "features.bin", root_ / "assets" / "features.bin");
        std::filesystem::copy_file(source_assets / "context.bin", root_ / "assets" / "context.bin");
    }

    ~TemporaryFixture() {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }

    std::filesystem::path WriteManifest(const std::string& name, const std::string& text) const {
        const std::filesystem::path path = root_ / name;
        std::ofstream stream(path, std::ios::binary);
        Require(stream.good(), "failed to create mutated manifest");
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        Require(stream.good(), "failed to write mutated manifest");
        return path;
    }

private:
    std::filesystem::path root_;
};

void RequireManifestFailure(const TemporaryFixture& fixture, const std::string& name,
                            const std::string& text,
                            const std::string& expected_profile = "framework_smoke") {
    npu_avs::ProfileManifest ignored_manifest;
    std::string error;
    const std::filesystem::path path = fixture.WriteManifest(name, text);
    Require(!npu_avs::LoadProfileManifest(path.string(), expected_profile,
                                          ignored_manifest, error),
            "invalid manifest was accepted");
    Require(!error.empty(), "invalid manifest did not produce an error");
}

} // namespace

int main(int argc, char** argv) {
    try {
        Require(argc == 6,
                "usage: npu-contract-tests <framework> <kws01> <ic01> <ad01> <sww01>");
        Require(npu_avs::Sha256Hex("abc") ==
                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                "SHA-256 standard vector failed");
        npu_avs::ProfileManifest loaded_manifest;
        std::string error;
        Require(npu_avs::LoadProfileManifest(argv[1], "framework_smoke", loaded_manifest, error),
                "framework manifest failed to load");
        const npu_avs::ProfileSpec* profile = &loaded_manifest.profile;
        Require(profile->implemented, "framework profile is unavailable");
        Require(npu_avs::FindProfileSpec("kws01") != nullptr, "kws01 is not registered");
        Require(loaded_manifest.manifest_sha256.size() == 64U,
                "manifest SHA-256 is missing");
        Require(loaded_manifest.tensor_signature_sha256 ==
                npu_avs::ComputeTensorSignatureSha256(*profile),
                "tensor signature is unstable");

        npu_avs::JsonValue parsed_json;
        Require(npu_avs::ParseJson("{\"emoji\":\"\\uD83D\\uDE80\"}", parsed_json, error),
                "valid Unicode surrogate pair was rejected");
        Require(!npu_avs::ParseJson("{\"duplicate\":1,\"duplicate\":2}", parsed_json, error),
                "duplicate JSON key was accepted");
        const std::string invalid_utf8 = std::string("{\"text\":\"") +
                                         static_cast<char>(0xc0U) +
                                         static_cast<char>(0xafU) + "\"}";
        Require(!npu_avs::ParseJson(invalid_utf8, parsed_json, error),
                "invalid UTF-8 was accepted");

        npu_avs::ProfileManifest repeated_manifest;
        Require(npu_avs::LoadProfileManifest(argv[1], "framework_smoke",
                                              repeated_manifest, error),
                "second framework manifest load failed");
        Require(repeated_manifest.manifest_sha256 == loaded_manifest.manifest_sha256 &&
                repeated_manifest.tensor_signature_sha256 ==
                    loaded_manifest.tensor_signature_sha256,
                "manifest hashes changed across loads");

        const char* profile_ids[] = {"kws01", "ic01", "ad01", "sww01"};
        for (int argument = 2; argument < argc; ++argument) {
            npu_avs::ProfileManifest workload_manifest;
            const char* profile_id = profile_ids[argument - 2];
            Require(npu_avs::LoadProfileManifest(argv[argument], profile_id,
                                                  workload_manifest, error),
                    "MLCommons workload manifest failed to load");
            Require(workload_manifest.profile.implemented,
                    "MLCommons workload profile is not implemented");
            Require(workload_manifest.profile.model_format == "tflite" &&
                    !workload_manifest.profile.model_path.empty() &&
                    workload_manifest.profile.model_sha256.size() == 64U,
                    "MLCommons workload model metadata is incomplete");
            Require(workload_manifest.tensor_signature_sha256 ==
                    npu_avs::ComputeTensorSignatureSha256(workload_manifest.profile),
                    "MLCommons tensor signature is unstable");

            npu_avs::WorkloadConfig workload_cfg;
            Require(npu_avs::ApplyProfileDefaults(profile_id, workload_cfg),
                    "MLCommons profile defaults failed");
            workload_cfg.backend = "null";
            npu_avs::NullBackend workload_backend;
            Require(workload_backend.Init(workload_cfg, workload_manifest.profile, error) ==
                    npu_avs::BackendStatus::Ok,
                    "null backend rejected an MLCommons profile");
            Require(workload_backend.CreateResources(error) == npu_avs::BackendStatus::Ok,
                    "null backend resource creation failed for an MLCommons profile");
            Require(workload_backend.SetInputs(workload_manifest.inputs, error) ==
                    npu_avs::BackendStatus::Ok,
                    "null backend rejected an MLCommons input tensor");
            Require(workload_backend.SubmitInference(0, error) == npu_avs::BackendStatus::Ok,
                    "null backend submission failed for an MLCommons profile");
            Require(workload_backend.WaitForCompletion(1000U, error) ==
                    npu_avs::BackendStatus::Ok,
                    "null backend wait failed for an MLCommons profile");
            npu_avs::InferenceResult workload_result;
            Require(workload_backend.ReadOutputs(workload_result, error) ==
                    npu_avs::BackendStatus::Ok,
                    "null backend output read failed for an MLCommons profile");
            Require(npu_avs::ValidateTensorSet(workload_result.outputs,
                                               workload_manifest.profile.outputs, error),
                    "null backend output contract failed for an MLCommons profile");
            Require(workload_backend.Destroy(50U, error) == npu_avs::BackendStatus::Ok,
                    "null backend teardown failed for an MLCommons profile");
        }

        const std::filesystem::path source_manifest =
            std::filesystem::absolute(std::filesystem::u8path(argv[1]));
        const std::string valid_text = ReadText(source_manifest);
        TemporaryFixture fixture(source_manifest);

        std::string duplicate_key = valid_text;
        ReplaceOnce(duplicate_key, "\"schema_version\": 1,",
                    "\"schema_version\": 1,\n  \"schema_version\": 1,");
        RequireManifestFailure(fixture, "duplicate-key.json", duplicate_key);

        std::string unknown_field = valid_text;
        ReplaceOnce(unknown_field, "{\n", "{\n  \"unexpected\": true,\n");
        RequireManifestFailure(fixture, "unknown-field.json", unknown_field);

        std::string bad_hash = valid_text;
        ReplaceOnce(bad_hash,
                    "2fe71e1bfad87e29801eeafefbf477c3e6581cfaf901f56da29f35c4ba95577d",
                    std::string(64U, '0'));
        RequireManifestFailure(fixture, "bad-hash.json", bad_hash);

        std::string traversal = valid_text;
        ReplaceOnce(traversal, "\"file\": \"features.bin\"",
                    "\"file\": \"../outside.bin\"");
        RequireManifestFailure(fixture, "path-traversal.json", traversal);

        std::string unapproved_profile = valid_text;
        ReplaceOnce(unapproved_profile, "\"id\": \"framework_smoke\"",
                    "\"id\": \"kws01\"");
        ReplaceOnce(unapproved_profile, "\"workload\": \"framework_smoke\"",
                    "\"workload\": \"kws01\"");
        RequireManifestFailure(fixture, "unapproved-profile.json",
                               unapproved_profile, "kws01");

        npu_avs::WorkloadConfig cfg;
        Require(npu_avs::ApplyProfileDefaults("framework_smoke", cfg), "profile defaults failed");
        cfg.backend = "null";

        npu_avs::NullBackend backend;
        Require(backend.SupportsProfile(*profile), "null backend should support framework profile");
        Require(backend.Init(cfg, *profile, error) == npu_avs::BackendStatus::Ok,
                "backend initialization failed");
        Require(backend.CreateResources(error) == npu_avs::BackendStatus::Ok,
                "resource creation failed");

        npu_avs::InferenceResult result;
        Require(backend.ReadOutputs(result, error) == npu_avs::BackendStatus::Error,
                "read before submission should fail");

        npu_avs::TensorSet inputs = loaded_manifest.inputs;
        npu_avs::TensorSet invalid_inputs = inputs;
        invalid_inputs.front().spec.name = "wrong_name";
        Require(backend.SetInputs(invalid_inputs, error) == npu_avs::BackendStatus::Error,
                "descriptor mismatch should fail");
        Require(backend.SetInputs(inputs, error) == npu_avs::BackendStatus::Ok,
                "valid inputs were rejected");
        Require(backend.SubmitInference(0, error) == npu_avs::BackendStatus::Ok,
                "submission failed");
        Require(backend.SubmitInference(1, error) == npu_avs::BackendStatus::Error,
                "overlapping submission should fail");
        Require(backend.WaitForCompletion(1000U, error) == npu_avs::BackendStatus::Ok,
                "wait failed");
        Require(backend.ReadOutputs(result, error) == npu_avs::BackendStatus::Ok,
                "output read failed");
        Require(npu_avs::ValidateTensorSet(result.outputs, profile->outputs, error),
                "output descriptors are invalid");
        Require(result.outputs.size() == 2U, "multi-output contract was not exercised");
        Require(backend.ReadOutputs(result, error) == npu_avs::BackendStatus::Error,
                "second output read should fail");
        Require(backend.Destroy(50U, error) == npu_avs::BackendStatus::Ok,
                "normal teardown failed");

        npu_avs::TensorSpec overflow;
        overflow.name = "overflow";
        overflow.data_type = npu_avs::TensorDataType::Float32;
        overflow.shape = {std::numeric_limits<uint32_t>::max(),
                          std::numeric_limits<uint32_t>::max()};
        uint64_t ignored_bytes = 0;
        Require(!npu_avs::TensorByteSize(overflow, ignored_bytes, error),
                "byte-size overflow should be rejected");

        cfg.simulated_latency_ms = 3000U;
        npu_avs::NullBackend timeout_backend;
        Require(timeout_backend.Init(cfg, *profile, error) == npu_avs::BackendStatus::Ok,
                "timeout backend initialization failed");
        Require(timeout_backend.CreateResources(error) == npu_avs::BackendStatus::Ok,
                "timeout resource creation failed");
        Require(timeout_backend.SetInputs(inputs, error) == npu_avs::BackendStatus::Ok,
                "timeout inputs failed");
        Require(timeout_backend.SubmitInference(0, error) == npu_avs::BackendStatus::Ok,
                "timeout submission failed");
        const auto timeout_start = std::chrono::steady_clock::now();
        Require(timeout_backend.WaitForCompletion(1U, error) == npu_avs::BackendStatus::Timeout,
                "inference did not time out");
        const npu_avs::BackendStatus destroy_status = timeout_backend.Destroy(50U, error);
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - timeout_start).count();
        Require(destroy_status == npu_avs::BackendStatus::Ok, "cancelled teardown failed");
        Require(elapsed_ms < 1000, "timeout teardown was not bounded");

        std::cout << "npu contract tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "npu contract test failure: " << exception.what() << '\n';
        return 1;
    }
}
