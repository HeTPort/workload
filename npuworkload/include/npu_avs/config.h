#pragma once

#include <cstdint>
#include <string>

namespace npu_avs {

struct WorkloadConfig {
    std::string profile = "framework_smoke";
    std::string api = "npu";
    std::string mode = "inference";
    std::string backend = "reference_cpu";
    std::string workload = "framework_smoke";
    std::string model_path;
    std::string input_manifest;

    double duration_s = 60.0;
    uint64_t inferences = 0;
    uint64_t warmup_inferences = 1;
    double timeout_s = 95.0;
    uint32_t inference_timeout_ms = 5000;
    uint32_t simulated_latency_ms = 0;

    uint64_t seed = 0x123456789abcdef0ULL;

    std::string verify_mode = "checksum";
    uint32_t verify_interval = 1;
    std::string golden_checksum;
    bool fail_fast = true;
    bool generate_golden = false;

    double heartbeat_interval_s = 1.0;
    std::string output_format = "jsonl";
    std::string output_path;
    bool summary_only = false;
    bool per_inference_log = false;

    std::string config_path;
    bool list_profiles = false;
    bool dump_effective_config = false;
    bool show_help = false;
    bool show_version = false;
};

bool ParseCommandLine(int argc, char** argv, WorkloadConfig& cfg, std::string& error);
bool LoadConfigFile(const std::string& path, WorkloadConfig& cfg, std::string& error);
bool ValidateConfig(const WorkloadConfig& cfg, std::string& error);
void DumpEffectiveConfig(const WorkloadConfig& cfg);
void PrintHelp();
void PrintVersion();

} // namespace npu_avs
