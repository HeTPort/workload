#include "cpu_avs/config.h"

#include "cpu_avs/profile.h"
#include "cpu_avs/utils.h"

#include <cmath>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace cpu_avs {
namespace {

bool LooksLikeOption(const std::string& value) {
    return value == "-h" || value.rfind("--", 0) == 0;
}

bool IsAlwaysFlag(const std::string& key) {
    return key == "--help" || key == "-h" || key == "--version" ||
           key == "--list-profiles" || key == "--dump-effective-config";
}

bool IsOptionalBoolFlag(const std::string& key) {
    return key == "--summary-only" || key == "--per-batch-log" ||
           key == "--per-frame-log" || key == "--generate-golden" ||
           key == "--fail-on-instability";
}

std::unordered_map<std::string, std::string> ParseCliMap(
    int argc, char** argv, std::string& error) {
    std::unordered_map<std::string, std::string> values;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        if (!LooksLikeOption(key)) {
            error = "unexpected argument: " + key;
            return {};
        }
        if (IsAlwaysFlag(key)) {
            values[key] = "true";
            continue;
        }
        if (IsOptionalBoolFlag(key)) {
            if (i + 1 < argc) {
                const std::string next = argv[i + 1];
                bool parsed = false;
                if (!LooksLikeOption(next) && ParseBool(next, parsed)) {
                    values[key] = next;
                    ++i;
                    continue;
                }
            }
            values[key] = "true";
            continue;
        }
        if (i + 1 >= argc || LooksLikeOption(argv[i + 1])) {
            error = "missing value for argument: " + key;
            return {};
        }
        values[key] = argv[++i];
    }
    return values;
}

uint32_t ToU32(const std::string& value) {
    if (!value.empty() && value.front() == '-') {
        throw std::invalid_argument("negative value is not allowed: " + value);
    }
    size_t position = 0;
    const unsigned long parsed = std::stoul(value, &position, 0);
    if (position != value.size() || parsed > std::numeric_limits<uint32_t>::max()) {
        throw std::invalid_argument("invalid uint32 value: " + value);
    }
    return static_cast<uint32_t>(parsed);
}

uint64_t ToU64(const std::string& value) {
    if (!value.empty() && value.front() == '-') {
        throw std::invalid_argument("negative value is not allowed: " + value);
    }
    size_t position = 0;
    const unsigned long long parsed = std::stoull(value, &position, 0);
    if (position != value.size()) {
        throw std::invalid_argument("invalid uint64 value: " + value);
    }
    return static_cast<uint64_t>(parsed);
}

double ToDouble(const std::string& value) {
    size_t position = 0;
    const double parsed = std::stod(value, &position);
    if (position != value.size() || !std::isfinite(parsed)) {
        throw std::invalid_argument("invalid floating-point value: " + value);
    }
    return parsed;
}

void ApplyBool(bool& destination, const std::string& value) {
    bool parsed = false;
    if (!ParseBool(value, parsed)) {
        throw std::invalid_argument("invalid boolean value: " + value);
    }
    destination = parsed;
}

bool ApplyKeyValue(WorkloadConfig& cfg, const std::string& key, const std::string& value) {
    if (key == "profile") cfg.profile = value;
    else if (key == "api") cfg.api = value;
    else if (key == "mode") cfg.mode = value;
    else if (key == "backend" || key == "kernel" || key == "shader") cfg.backend = value;
    else if (key == "complexity") cfg.complexity = value;
    else if (key == "config") cfg.config_path = value;

    else if (key == "duration") cfg.duration_s = ToDouble(value);
    else if (key == "batches" || key == "frames") cfg.batches = ToU64(value);
    else if (key == "warmup") cfg.warmup_s = ToDouble(value);
    else if (key == "timeout") cfg.timeout_s = ToDouble(value);
    else if (key == "loop") ApplyBool(cfg.loop, value);
    else if (key == "iterations" || key == "batch-iterations" || key == "batch_iterations") cfg.iterations = ToU64(value);
    else if (key == "threads" || key == "thread-count" || key == "thread_count") cfg.threads = ToU32(value);
    else if (key == "working-set-kb" || key == "working_set_kb") cfg.working_set_kb = ToU32(value);
    else if (key == "seed") cfg.seed = ToU64(value);
    else if (key == "duty-cycle" || key == "duty_cycle") cfg.duty_cycle = ToDouble(value);
    else if (key == "burst-period" || key == "burst_period") cfg.burst_period_s = ToDouble(value);
    else if (key == "burst-active" || key == "burst_active") cfg.burst_active_s = ToDouble(value);

    else if (key == "verify-mode" || key == "verify_mode") cfg.verify_mode = value;
    else if (key == "checksum-interval" || key == "checksum_interval") cfg.checksum_interval = ToU32(value);
    else if (key == "golden-checksum" || key == "golden_checksum") cfg.golden_checksum = value;
    else if (key == "fail-fast" || key == "fail_fast") ApplyBool(cfg.fail_fast, value);
    else if (key == "generate-golden" || key == "generate_golden") ApplyBool(cfg.generate_golden, value);
    else if (key == "batch-timeout-ms" || key == "batch_timeout_ms" || key == "gpu-timeout-ms" || key == "gpu_timeout_ms") cfg.batch_timeout_ms = ToU32(value);

    else if (key == "heartbeat-interval" || key == "heartbeat_interval") cfg.heartbeat_interval_s = ToDouble(value);
    else if (key == "output-format" || key == "output_format") cfg.output_format = value;
    else if (key == "output") cfg.output_path = value;
    else if (key == "summary-only" || key == "summary_only") ApplyBool(cfg.summary_only, value);
    else if (key == "per-batch-log" || key == "per_batch_log" || key == "per-frame-log" || key == "per_frame_log") ApplyBool(cfg.per_batch_log, value);
    else if (key == "log-level" || key == "log_level") cfg.log_level = value;

    else if (key == "fail-on-instability" || key == "fail_on_instability") ApplyBool(cfg.fail_on_instability, value);
    else if (key == "min-operations-per-sec" || key == "min_operations_per_sec") cfg.min_operations_per_sec = ToDouble(value);
    else if (key == "max-throughput-cv-pct" || key == "max_throughput_cv_pct") cfg.max_throughput_cv_pct = ToDouble(value);
    else if (key == "max-batch-p99-ms" || key == "max_batch_p99_ms") cfg.max_batch_p99_ms = ToDouble(value);
    else if (key == "max-heartbeat-gap" || key == "max_heartbeat_gap_s") cfg.max_heartbeat_gap_s = ToDouble(value);

    else if (key == "width") cfg.width = ToU32(value);
    else if (key == "height") cfg.height = ToU32(value);
    else if (key == "rt-format" || key == "rt_format") cfg.rt_format = value;
    else if (key == "samples") cfg.samples = ToU32(value);
    else if (key == "texture-count" || key == "texture_count") cfg.texture_count = ToU32(value);
    else if (key == "texture-size" || key == "texture_size") cfg.texture_size = value;
    else if (key == "gpu-timestamp" || key == "gpu_timestamp") ApplyBool(cfg.gpu_timestamp, value);
    else if (key == "shader-dir" || key == "shader_dir" || key == "timestamp-scope" ||
             key == "timestamp_scope" || key == "golden-file" || key == "golden_file" ||
             key == "pixel-threshold" || key == "pixel_threshold" ||
             key == "pixel-max-diff-count" || key == "pixel_max_diff_count") {
        // Accepted legacy GPU-only option; intentionally ignored by the CPU workload.
    } else return false;
    return true;
}

std::string JsonEscape(const std::string& value) {
    std::string result;
    for (char c : value) {
        switch (c) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c; break;
        }
    }
    return result;
}

} // namespace

bool LoadConfigFile(const std::string& path, WorkloadConfig& cfg, std::string& error) {
    std::ifstream input(path);
    if (!input) {
        error = "failed to open config file: " + path;
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    const std::string text = buffer.str();
    const std::regex key_value(
        "\"([A-Za-z0-9_\\-]+)\"\\s*:\\s*"
        "(\"([^\"]*)\"|true|false|null|-?[0-9]+(?:\\.[0-9]+)?(?:[eE][+-]?[0-9]+)?)");

    try {
        for (auto it = std::sregex_iterator(text.begin(), text.end(), key_value);
             it != std::sregex_iterator(); ++it) {
            const std::string key = (*it)[1].str();
            const std::string raw = (*it)[2].str();
            std::string value = raw;
            if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
                value = raw.substr(1, raw.size() - 2);
            } else if (raw == "null") {
                value.clear();
            }
            if (!ApplyKeyValue(cfg, key, value)) {
                error = "unknown key in config file '" + path + "': " + key;
                return false;
            }
        }
    } catch (const std::exception& exception) {
        error = "failed to parse config file '" + path + "': " + exception.what();
        return false;
    }
    return true;
}

bool ValidateConfig(const WorkloadConfig& cfg, std::string& error) {
    if (cfg.api != "cpu") {
        error = "unsupported api for CPU workload: " + cfg.api;
        return false;
    }
    if (cfg.mode != "compute") {
        error = "unsupported mode for CPU workload: " + cfg.mode;
        return false;
    }
    if (cfg.duration_s <= 0.0 && cfg.batches == 0) {
        error = "duration or batches must specify a positive stop condition";
        return false;
    }
    if (cfg.warmup_s < 0.0 || cfg.timeout_s <= 0.0) {
        error = "warmup must be non-negative and timeout must be positive";
        return false;
    }
    if (cfg.iterations == 0 || cfg.threads == 0 || cfg.threads > 256 || cfg.working_set_kb == 0) {
        error = "iterations and working-set-kb must be positive; threads must be in [1,256]";
        return false;
    }
    if (static_cast<uint64_t>(cfg.threads) * cfg.working_set_kb > 1024ULL * 1024ULL) {
        error = "aggregate working set must not exceed 1 GiB";
        return false;
    }
    if (cfg.duty_cycle < 0.0 || cfg.duty_cycle > 1.0 || cfg.burst_period_s < 0.0 ||
        cfg.burst_active_s < 0.0 || cfg.burst_active_s > cfg.burst_period_s) {
        error = "invalid duty-cycle or burst timing";
        return false;
    }
    if (cfg.output_format != "jsonl") {
        error = "only jsonl output is supported";
        return false;
    }
    if (cfg.verify_mode != "none" && cfg.verify_mode != "checksum" && cfg.verify_mode != "crc") {
        error = "unsupported verify mode: " + cfg.verify_mode;
        return false;
    }
    if (!cfg.golden_checksum.empty()) {
        std::string checksum = cfg.golden_checksum;
        if (checksum.rfind("0x", 0) == 0 || checksum.rfind("0X", 0) == 0) checksum.erase(0, 2);
        const size_t maximum_length = cfg.verify_mode == "crc" ? 8U : 16U;
        if (checksum.empty() || checksum.size() > maximum_length ||
            !std::all_of(checksum.begin(), checksum.end(), [](unsigned char c) { return std::isxdigit(c) != 0; })) {
            error = "golden-checksum has an invalid hexadecimal width for the selected verify mode";
            return false;
        }
    }
    if (cfg.min_operations_per_sec < 0.0 || cfg.max_throughput_cv_pct < 0.0 ||
        cfg.max_batch_p99_ms < 0.0 || cfg.max_heartbeat_gap_s < 0.0) {
        error = "performance thresholds must be non-negative";
        return false;
    }
    return true;
}

bool ParseCommandLine(int argc, char** argv, WorkloadConfig& cfg, std::string& error) {
    const auto cli = ParseCliMap(argc, argv, error);
    if (!error.empty()) return false;
    if (cli.count("--help") || cli.count("-h")) {
        cfg.show_help = true;
        return true;
    }
    if (cli.count("--version")) {
        cfg.show_version = true;
        return true;
    }

    const bool list_profiles = cli.count("--list-profiles") != 0;
    const bool dump_config = cli.count("--dump-effective-config") != 0;
    const std::string config_path = cli.count("--config") ? cli.at("--config") : "";

    WorkloadConfig probe;
    if (!config_path.empty() && !LoadConfigFile(config_path, probe, error)) return false;
    const std::string selected_profile = cli.count("--profile") ? cli.at("--profile") : probe.profile;

    WorkloadConfig effective;
    if (!ApplyProfileDefaults(selected_profile, effective)) {
        error = "unknown profile: " + selected_profile;
        return false;
    }
    effective.config_path = config_path;
    if (!config_path.empty() && !LoadConfigFile(config_path, effective, error)) return false;

    try {
        for (const auto& entry : cli) {
            const std::string& raw_key = entry.first;
            if (IsAlwaysFlag(raw_key) || raw_key == "--config") continue;
            std::string key = raw_key.rfind("--", 0) == 0 ? raw_key.substr(2) : raw_key;
            if (!ApplyKeyValue(effective, key, entry.second)) {
                error = "unknown command line argument: " + raw_key;
                return false;
            }
        }
    } catch (const std::exception& exception) {
        error = std::string("failed to parse command line: ") + exception.what();
        return false;
    }

    effective.list_profiles = list_profiles;
    effective.dump_effective_config = dump_config;
    if (effective.timeout_s <= 0.0) {
        effective.timeout_s = effective.warmup_s + effective.duration_s + 30.0;
    }
    if (!ValidateConfig(effective, error)) return false;
    cfg = effective;
    return true;
}

void DumpEffectiveConfig(const WorkloadConfig& cfg) {
    std::cout << "{"
              << "\"profile\":\"" << JsonEscape(cfg.profile) << "\","
              << "\"api\":\"" << JsonEscape(cfg.api) << "\","
              << "\"mode\":\"" << JsonEscape(cfg.mode) << "\","
              << "\"backend\":\"" << JsonEscape(cfg.backend) << "\","
              << "\"duration_s\":" << cfg.duration_s << ','
              << "\"batches\":" << cfg.batches << ','
              << "\"warmup_s\":" << cfg.warmup_s << ','
              << "\"timeout_s\":" << cfg.timeout_s << ','
              << "\"iterations\":" << cfg.iterations << ','
              << "\"threads\":" << cfg.threads << ','
              << "\"working_set_kb\":" << cfg.working_set_kb << ','
              << "\"seed\":" << cfg.seed << ','
              << "\"duty_cycle\":" << cfg.duty_cycle << ','
              << "\"burst_period_s\":" << cfg.burst_period_s << ','
              << "\"burst_active_s\":" << cfg.burst_active_s << ','
              << "\"verify_mode\":\"" << JsonEscape(cfg.verify_mode) << "\","
              << "\"checksum_interval\":" << cfg.checksum_interval << ','
              << "\"batch_timeout_ms\":" << cfg.batch_timeout_ms << ','
              << "\"heartbeat_interval_s\":" << cfg.heartbeat_interval_s << ','
              << "\"output_format\":\"" << JsonEscape(cfg.output_format) << "\","
              << "\"output\":\"" << JsonEscape(cfg.output_path) << "\","
              << "\"summary_only\":" << (cfg.summary_only ? "true" : "false") << ','
              << "\"per_batch_log\":" << (cfg.per_batch_log ? "true" : "false") << ','
              << "\"fail_on_instability\":" << (cfg.fail_on_instability ? "true" : "false") << ','
              << "\"min_operations_per_sec\":" << cfg.min_operations_per_sec << ','
              << "\"max_throughput_cv_pct\":" << cfg.max_throughput_cv_pct << ','
              << "\"max_batch_p99_ms\":" << cfg.max_batch_p99_ms
              << "}\n";
}

void PrintHelp() {
    std::cout << R"(cpu-avs-workload

Usage:
  cpu-avs-workload [options]

Core:
  --profile <name>               Select profile defaults
  --backend <null|integer|floating_point|matrix|memory|mixed>
  --api <cpu>                    Compatibility field
  --mode <compute>               Compatibility field
  --config <path>                Flat JSON configuration

Runtime:
  --duration <sec>               Measured duration
  --batches <count>              Stop after completed batches
  --frames <count>               Legacy alias for --batches
  --warmup <sec>
  --timeout <sec>
  --iterations <count>           Work per thread and batch
  --threads <count>
  --working-set-kb <KiB>         Per-thread data footprint
  --seed <integer>               Decimal or 0x-prefixed seed
  --duty-cycle <0..1>
  --burst-period <sec>
  --burst-active <sec>
  --batch-timeout-ms <ms>

Verification:
  --verify-mode <none|checksum|crc>
  --checksum-interval <batches>
  --golden-checksum <hex>
  --fail-fast <bool>
  --generate-golden[=<bool>]

Monitoring and output:
  --heartbeat-interval <sec>
  --output-format <jsonl>
  --output <path>
  --summary-only[=<bool>]
  --per-batch-log[=<bool>]
  --fail-on-instability[=<bool>]
  --min-operations-per-sec <value>
  --max-throughput-cv-pct <percent>
  --max-batch-p99-ms <ms>
  --max-heartbeat-gap <sec>

Utility:
  --list-profiles
  --dump-effective-config
  --help, -h
  --version
)";
}

void PrintVersion() {
    std::cout << "cpu-avs-workload 1.0.0\n";
}

} // namespace cpu_avs
