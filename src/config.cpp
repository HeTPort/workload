#include "gpu_avs/config.h"
#include "gpu_avs/profile.h"
#include "gpu_avs/utils.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace gpu_avs {

static bool IsAlwaysFlagOnlyArg(const std::string& key) {
    return key == "--list-profiles" ||
           key == "--dump-effective-config" ||
           key == "--help" ||
           key == "-h" ||
           key == "--version";
}

static bool IsOptionalBoolFlagArg(const std::string& key) {
    return key == "--summary-only" ||
           key == "--per-frame-log" ||
           key == "--generate-golden";
}

static bool LooksLikeOption(const std::string& s) {
    return s == "-h" || s.rfind("--", 0) == 0;
}

static bool IsBoolToken(const std::string& s) {
    bool out = false;
    return ParseBool(s, out);
}

static std::unordered_map<std::string, std::string> ParseCliMap(
    int argc,
    char** argv,
    std::string& error
) {
    std::unordered_map<std::string, std::string> m;

    for (int i = 1; i < argc; ++i) {
        std::string key = argv[i];

        if (!LooksLikeOption(key)) {
            error = "unexpected argument: " + key;
            return {};
        }

        if (IsAlwaysFlagOnlyArg(key)) {
            m[key] = "true";
            continue;
        }

        if (IsOptionalBoolFlagArg(key)) {
            if (i + 1 < argc) {
                std::string next = argv[i + 1];
                if (!LooksLikeOption(next) && IsBoolToken(next)) {
                    m[key] = next;
                    ++i;
                    continue;
                }
            }

            m[key] = "true";
            continue;
        }

        if (i + 1 >= argc) {
 error = "missing value for argument: " + key;
            return {};
        }

        std::string value = argv[i + 1];
        if (LooksLikeOption(value)) {
            error = "missing value for argument: " + key;
            return {};
        }

        m[key] = value;
        ++i;
    }

    return m;
}

static uint32_t ToU32(const std::string& v) {
    if (!v.empty() && v[0] == '-') {
        throw std::invalid_argument("negative value is not allowed: " + v);
    }

    size_t pos = 0;
    unsigned long n = std::stoul(v, &pos);

    if (pos != v.size()) {
        throw std::invalid_argument("invalid uint32 value: " + v);
    }

    if (n > std::numeric_limits<uint32_t>::max()) {
        throw std::out_of_range("uint32 value out of range: " + v);
    }

    return static_cast<uint32_t>(n);
}

static uint64_t ToU64(const std::string& v) {
    if (!v.empty() && v[0] == '-') {
        throw std::invalid_argument("negative value is not allowed: " + v);
    }

    size_t pos = 0;
    unsigned long long n = std::stoull(v, &pos);

    if (pos != v.size()) {
        throw std::invalid_argument("invalid uint64 value: " + v);
    }

    return static_cast<uint64_t>(n);
}

static double ToDouble(const std::string& v) {
    size_t pos = 0;
    double n = std::stod(v, &pos);

    if (pos != v.size()) {
        throw std::invalid_argument("invalid double value: " + v);
    }

    return n;
}

static void ApplyBoolValue(bool& dst, const std::string& value) {
    bool b = false;
    if (!ParseBool(value, b)) {
        throw std::invalid_argument("invalid boolean value: " + value);
    }
    dst = b;
}

static bool ApplyKeyValue(
    WorkloadConfig& cfg,
    const std::string& key,
    const std::string& value
) {
    if (key == "profile") cfg.profile = value;
    else if (key == "api") cfg.api = value;
    else if (key == "mode") cfg.mode = value;
    else if (key == "config") cfg.config_path = value;

    else if (key == "width") cfg.width = ToU32(value);
    else if (key == "height") cfg.height = ToU32(value);
    else if (key == "rt-format" || key == "rt_format") cfg.rt_format = value;
    else if (key == "samples") cfg.samples = ToU32(value);

    else if (key == "duration") cfg.duration_s = ToDouble(value);
    else if (key == "frames") cfg.frames = ToU64(value);
    else if (key == "warmup") cfg.warmup_s = ToDouble(value);
    else if (key == "timeout") cfg.timeout_s = ToDouble(value);
    else if (key == "loop") ApplyBoolValue(cfg.loop, value);

    else if (key == "shader") cfg.shader = value;
    else if (key == "shader-dir" || key == "shader_dir") cfg.shader_dir = value;
    else if (key == "complexity") cfg.complexity = value;
    else if (key == "iterations") cfg.iterations = ToU32(value);
    else if (key == "texture-count" || key == "texture_count") cfg.texture_count = ToU32(value);
    else if (key == "texture-size" || key == "texture_size") cfg.texture_size = value;
    else if (key == "duty-cycle" || key == "duty_cycle") cfg.duty_cycle = ToDouble(value);
    else if (key == "burst-period" || key == "burst_period") cfg.burst_period_s = ToDouble(value);
    else if (key == "burst-active" || key == "burst_active") cfg.burst_active_s = ToDouble(value);

    else if (key == "verify-mode" || key == "verify_mode") cfg.verify_mode = value;
    else if (key == "checksum-interval" || key == "checksum_interval") cfg.checksum_interval = ToU32(value);
    else if (key == "golden-checksum" || key == "golden_checksum") cfg.golden_checksum = value;
    else if (key == "golden-file" || key == "golden_file") cfg.golden_file = value;
    else if (key == "pixel-threshold" || key == "pixel_threshold") cfg.pixel_threshold = ToDouble(value);
    else if (key == "pixel-max-diff-count" || key == "pixel_max_diff_count") cfg.pixel_max_diff_count = ToU64(value);
    else if (key == "fail-fast" || key == "fail_fast") ApplyBoolValue(cfg.fail_fast, value);
    else if (key == "generate-golden" || key == "generate_golden") ApplyBoolValue(cfg.generate_golden, value);

    else if (key == "gpu-timestamp" || key == "gpu_timestamp") ApplyBoolValue(cfg.gpu_timestamp, value);
    else if (key == "timestamp-scope" || key == "timestamp_scope") cfg.timestamp_scope = value;
    else if (key == "gpu-timeout-ms" || key == "gpu_timeout_ms") cfg.gpu_timeout_ms = ToU32(value);

    else if (key == "heartbeat-interval" || key == "heartbeat_interval") cfg.heartbeat_interval_s = ToDouble(value);
    else if (key == "output-format" || key == "output_format") cfg.output_format = value;
    else if (key == "output") cfg.output_path = value;
    else if (key == "summary-only" || key == "summary_only") ApplyBoolValue(cfg.summary_only, value);
    else if (key == "per-frame-log" || key == "per_frame_log") ApplyBoolValue(cfg.per_frame_log, value);
    else if (key == "log-level" || key == "log_level") cfg.log_level = value;
    else return false;

    return true;
}

bool LoadConfigFile(const std::string& path, WorkloadConfig& cfg, std::string& error) {
    std::ifstream in(path);
    if (!in) {
        error = "failed to open config file: " + path;
        return false;
    }

    std::stringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();

    std::regex kv_regex(
        "\"([A-Za-z0-9_\\-]+)\"\\s*:\\s*"
        "(\"([^\"]*)\"|true|false|null|-?[0-9]+(\\.[0-9]+)?)"
    );

    auto begin = std::sregex_iterator(text.begin(), text.end(), kv_regex);
    auto end = std::sregex_iterator();

    try {
        for (auto it = begin; it != end; ++it) {
            std::string key = (*it)[1].str();
            std::string raw = (*it)[2].str();
            std::string val = raw;

            if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
                val = raw.substr(1, raw.size() - 2);
            }

            if (val == "null") {
                val.clear();
            }

            if (!ApplyKeyValue(cfg, key, val)) {
                error = "unknown key in config file '" + path + "': " + key;
                return false;
            }
        }
    } catch (const std::exception& e) {
        error = "failed to parse config file '" + path + "': " + e.what();
        return false;
    }

    return true;
}

bool ParseCommandLine(int argc, char** argv, WorkloadConfig& cfg, std::string& error) {
    auto cli = ParseCliMap(argc, argv, error);
    if (!error.empty()) {
        return false;
    }

    if (cli.count("--help") || cli.count("-h")) {
        cfg.show_help = true;
        return true;
    }

    if (cli.count("--version")) {
        cfg.show_version = true;
        return true;
    }

    cfg.list_profiles = cli.count("--list-profiles") > 0;
    cfg.dump_effective_config = cli.count("--dump-effective-config") > 0;

    std::string config_path;
    if (cli.count("--config")) {
        config_path = cli["--config"];
    }

    WorkloadConfig config_probe;
    if (!config_path.empty()) {
        config_probe.config_path = config_path;
        if (!LoadConfigFile(config_path, config_probe, error)) {
            return false;
        }
    }

    std::string selected_profile = config_probe.profile;
    if (cli.count("--profile")) {
        selected_profile = cli["--profile"];
    }

    WorkloadConfig effective;
    if (!ApplyProfileDefaults(selected_profile, effective)) {
        error = "unknown profile: " + selected_profile;
        return false;
    }

    effective.config_path = config_path;

    if (!config_path.empty()) {
        if (!LoadConfigFile(config_path, effective, error)) {
            return false;
        }
    }

    try {
        for (const auto& [raw_key, value] : cli) {
            if (raw_key == "--help" ||
                raw_key == "-h" ||
                raw_key == "--version" ||
                raw_key == "--list-profiles" ||
                raw_key == "--dump-effective-config" ||
                raw_key == "--config") {
                continue;
            }

            std::string key = raw_key;
            if (key.rfind("--", 0) == 0) {
                key = key.substr(2);
            }

            if (!ApplyKeyValue(effective, key, value)) {
                error = "unknown command line argument: " + raw_key;
                return false;
            }
        }
    } catch (const std::exception& e) {
        error = std::string("failed to parse command line: ") + e.what();
        return false;
    }

    effective.list_profiles = cfg.list_profiles;
    effective.dump_effective_config = cfg.dump_effective_config;
    effective.show_help = cfg.show_help;
    effective.show_version = cfg.show_version;

    if (effective.timeout_s <= 0.0) {
        effective.timeout_s = effective.warmup_s + effective.duration_s + 30.0;
    }

    cfg = effective;
    return true;
}

void PrintHelp() {
    std::cout <<
R"(gpu-avs-workload

Usage:
  gpu-avs-workload [options]

Core:
  --profile <profile>
  --api <null|gles|vulkan|opencl>
  --mode <offscreen|onscreen|compute>
  --config <path>

Resolution:
  --width <int>
  --height <int>
  --rt-format <RGBA8|RGBA16F|RGBA32F>
  --samples <int>

Runtime:
  --duration <sec>
  --frames <count>
  --warmup <sec>
  --timeout <sec>
  --loop <true|false>

Load:
  --shader <none|alu|sfu|texture|fill|mixed|branch|compute_mem>
  --shader-dir <path>
  --complexity <low|medium|high|extreme|int>
  --iterations <int>
  --texture-count <int>
  --texture-size <WxH>
  --duty-cycle <float>
  --burst-period <sec>
  --burst-active <sec>

Verify:
  --verify-mode <none|crc|checksum|golden-image|pixel-diff|compute-compare>
  --checksum-interval <N>
  --golden-checksum <hex>
  --golden-file <path>
  --pixel-threshold <value>
  --pixel-max-diff-count <count>
  --fail-fast <true|false>
  --generate-golden [true|false]

Timestamp:
  --gpu-timestamp <true|false>
  --timestamp-scope <frame|dispatch|draw|pass>
  --gpu-timeout-ms <ms>

Output:
  --heartbeat-interval <sec>
  --output-format <text|csv|json|jsonl>
  --output <path>
  --summary-only [true|false]
  --per-frame-log [true|false]
  --log-level <error|warn|info|debug>

Utility:
  --list-profiles
  --dump-effective-config
  --help
  --version
)";
}

void PrintVersion() {
    std::cout << "gpu-avs-workload version 0.5.0\n";
}

} // namespace gpu_avs
