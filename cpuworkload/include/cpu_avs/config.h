#pragma once

#include <cstdint>
#include <string>

namespace cpu_avs {

struct WorkloadConfig {
    std::string profile = "mixed";
    std::string api = "cpu";
    std::string mode = "compute";
    std::string backend = "mixed";
    std::string complexity = "medium";

    double duration_s = 60.0;
    uint64_t batches = 0;
    double warmup_s = 5.0;
    double timeout_s = 95.0;
    bool loop = true;

    uint64_t iterations = 20000;
    uint32_t threads = 1;
    uint32_t working_set_kb = 256;
    uint64_t seed = 0x123456789abcdef0ULL;

    double duty_cycle = 1.0;
    double burst_period_s = 2.0;
    double burst_active_s = 1.0;

    std::string verify_mode = "checksum";
    uint32_t checksum_interval = 1;
    std::string golden_checksum;
    bool fail_fast = true;
    bool generate_golden = false;

    uint32_t batch_timeout_ms = 5000;

    double heartbeat_interval_s = 1.0;
    std::string output_format = "jsonl";
    std::string output_path;
    bool summary_only = false;
    bool per_batch_log = false;
    std::string log_level = "info";

    bool fail_on_instability = false;
    double min_operations_per_sec = 0.0;
    double max_throughput_cv_pct = 0.0;
    double max_batch_p99_ms = 0.0;
    double max_heartbeat_gap_s = 0.0;

    // Accepted legacy GPU arguments. They are recorded but do not affect CPU work.
    uint32_t width = 0;
    uint32_t height = 0;
    std::string rt_format;
    uint32_t samples = 1;
    uint32_t texture_count = 0;
    std::string texture_size;
    bool gpu_timestamp = false;

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

} // namespace cpu_avs
