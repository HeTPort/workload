#pragma once

#include <cstdint>
#include <string>

namespace gpu_avs {

struct WorkloadConfig {
    std::string profile = "game_mid";
    std::string api = "null";
    std::string mode = "offscreen";

    uint32_t width = 1920;
    uint32_t height = 1080;
    std::string rt_format = "RGBA8";
    uint32_t samples = 1;

    double duration_s = 60.0;
    uint64_t frames = 0;
    double warmup_s = 5.0;
    double timeout_s = 95.0;
    bool loop = true;

    std::string shader = "mixed";
    std::string shader_dir;
    std::string complexity = "medium";
    uint32_t iterations = 128;
    uint32_t texture_count = 4;
    std::string texture_size = "2048x2048";
    double duty_cycle = 1.0;
    double burst_period_s = 2.0;
    double burst_active_s = 1.0;

    std::string verify_mode = "none";
    uint32_t checksum_interval = 60;
    std::string golden_checksum;
    std::string golden_file;
    double pixel_threshold = 0.0;
    uint64_t pixel_max_diff_count = 0;
    bool fail_fast = true;
    bool generate_golden = false;

    bool gpu_timestamp = false;
    std::string timestamp_scope = "frame";
    uint32_t gpu_timeout_ms = 5000;

    double heartbeat_interval_s = 1.0;
    std::string output_format = "jsonl";
    std::string output_path;
    bool summary_only = false;
    bool per_frame_log = false;
    std::string log_level = "info";

    std::string config_path;

    bool list_profiles = false;
    bool dump_effective_config = false;
    bool show_help = false;
    bool show_version = false;
};

bool ParseCommandLine(int argc, char** argv, WorkloadConfig& cfg, std::string& error);
bool LoadConfigFile(const std::string& path, WorkloadConfig& cfg, std::string& error);

void PrintHelp();
void PrintVersion();

} // namespace gpu_avs
