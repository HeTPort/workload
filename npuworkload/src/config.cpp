#include "npu_avs/config.h"

#include "npu_avs/profile.h"
#include "npu_avs/utils.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <fstream>
#include <iostream>
#include <limits>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace npu_avs {
namespace {

bool LooksLikeOption(const std::string& value) {
    return value == "-h" || value.rfind("--", 0) == 0;
}

bool IsAlwaysFlag(const std::string& key) {
    return key == "--help" || key == "-h" || key == "--version" ||
           key == "--list-profiles" || key == "--dump-effective-config";
}

bool IsOptionalBoolFlag(const std::string& key) {
    return key == "--summary-only" || key == "--per-inference-log" ||
           key == "--generate-golden";
}

std::unordered_map<std::string, std::string> ParseCliMap(int argc, char** argv,
                                                         std::string& error) {
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
                bool parsed = false;
                const std::string next = argv[i + 1];
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
    if (!value.empty() && value.front() == '-') throw std::invalid_argument("negative value: " + value);
    size_t position = 0;
    const unsigned long parsed = std::stoul(value, &position, 0);
    if (position != value.size() || parsed > std::numeric_limits<uint32_t>::max()) {
        throw std::invalid_argument("invalid uint32 value: " + value);
    }
    return static_cast<uint32_t>(parsed);
}

uint64_t ToU64(const std::string& value) {
    if (!value.empty() && value.front() == '-') throw std::invalid_argument("negative value: " + value);
    size_t position = 0;
    const unsigned long long parsed = std::stoull(value, &position, 0);
    if (position != value.size()) throw std::invalid_argument("invalid uint64 value: " + value);
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
    if (!ParseBool(value, parsed)) throw std::invalid_argument("invalid boolean value: " + value);
    destination = parsed;
}

bool ApplyKeyValue(WorkloadConfig& cfg, const std::string& key, const std::string& value) {
    if (key == "profile") cfg.profile = value;
    else if (key == "api") cfg.api = value;
    else if (key == "mode") cfg.mode = value;
    else if (key == "backend") cfg.backend = value;
    else if (key == "workload") cfg.workload = value;
    else if (key == "model" || key == "model_path") cfg.model_path = value;
    else if (key == "input-manifest" || key == "input_manifest") cfg.input_manifest = value;
    else if (key == "config") cfg.config_path = value;
    else if (key == "duration" || key == "duration_s") cfg.duration_s = ToDouble(value);
    else if (key == "inferences" || key == "batches" || key == "frames") cfg.inferences = ToU64(value);
    else if (key == "warmup-inferences" || key == "warmup_inferences") cfg.warmup_inferences = ToU32(value);
    else if (key == "timeout" || key == "timeout_s") cfg.timeout_s = ToDouble(value);
    else if (key == "inference-timeout-ms" || key == "inference_timeout_ms" ||
             key == "batch-timeout-ms" || key == "batch_timeout_ms") cfg.inference_timeout_ms = ToU32(value);
    else if (key == "simulated-latency-ms" || key == "simulated_latency_ms") cfg.simulated_latency_ms = ToU32(value);
    else if (key == "input-elements" || key == "input_elements") cfg.input_elements = ToU32(value);
    else if (key == "output-elements" || key == "output_elements") cfg.output_elements = ToU32(value);
    else if (key == "seed") cfg.seed = ToU64(value);
    else if (key == "verify-mode" || key == "verify_mode") cfg.verify_mode = value;
    else if (key == "verify-interval" || key == "verify_interval" ||
             key == "checksum-interval" || key == "checksum_interval") cfg.verify_interval = ToU32(value);
    else if (key == "golden-checksum" || key == "golden_checksum") cfg.golden_checksum = value;
    else if (key == "fail-fast" || key == "fail_fast") ApplyBool(cfg.fail_fast, value);
    else if (key == "generate-golden" || key == "generate_golden") ApplyBool(cfg.generate_golden, value);
    else if (key == "heartbeat-interval" || key == "heartbeat_interval" ||
             key == "heartbeat_interval_s") cfg.heartbeat_interval_s = ToDouble(value);
    else if (key == "output-format" || key == "output_format") cfg.output_format = value;
    else if (key == "output") cfg.output_path = value;
    else if (key == "summary-only" || key == "summary_only") ApplyBool(cfg.summary_only, value);
    else if (key == "per-inference-log" || key == "per_inference_log") ApplyBool(cfg.per_inference_log, value);
    else return false;
    return true;
}

std::string JsonEscape(const std::string& value) {
    std::string result;
    for (char c : value) {
        if (c == '\\') result += "\\\\";
        else if (c == '"') result += "\\\"";
        else if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else if (c == '\t') result += "\\t";
        else result += c;
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
    if (cfg.api != "npu") { error = "unsupported api for NPU workload: " + cfg.api; return false; }
    if (cfg.mode != "inference") { error = "unsupported mode for NPU workload: " + cfg.mode; return false; }
    if (cfg.duration_s <= 0.0 && cfg.inferences == 0) {
        error = "duration or inferences must specify a positive stop condition";
        return false;
    }
    if (cfg.timeout_s <= 0.0 || cfg.inference_timeout_ms == 0) {
        error = "timeout and inference-timeout-ms must be positive";
        return false;
    }
    if (cfg.input_elements == 0 || cfg.output_elements == 0 ||
        cfg.input_elements > 1024U * 1024U || cfg.output_elements > 1024U * 1024U) {
        error = "tensor element counts must be in [1,1048576]";
        return false;
    }
    if (cfg.output_format != "jsonl") { error = "only jsonl output is supported"; return false; }
    if (cfg.verify_mode != "none" && cfg.verify_mode != "checksum" && cfg.verify_mode != "crc") {
        error = "unsupported verify mode: " + cfg.verify_mode;
        return false;
    }
    if (cfg.verify_mode != "none" && cfg.verify_interval == 0 && !cfg.generate_golden) {
        error = "verify-interval must be positive when verification is enabled";
        return false;
    }
    if (static_cast<uint64_t>(cfg.input_elements) * cfg.output_elements > 100000000ULL) {
        error = "synthetic tensor shape exceeds the 100 million operation-pair safety limit";
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
    return true;
}

bool ParseCommandLine(int argc, char** argv, WorkloadConfig& cfg, std::string& error) {
    const auto cli = ParseCliMap(argc, argv, error);
    if (!error.empty()) return false;
    if (cli.count("--help") || cli.count("-h")) { cfg.show_help = true; return true; }
    if (cli.count("--version")) { cfg.show_version = true; return true; }

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
            const std::string key = raw_key.rfind("--", 0) == 0 ? raw_key.substr(2) : raw_key;
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
    if (!ValidateConfig(effective, error)) return false;
    cfg = effective;
    return true;
}

void DumpEffectiveConfig(const WorkloadConfig& cfg) {
    std::cout << "{\"profile\":\"" << JsonEscape(cfg.profile)
              << "\",\"api\":\"" << JsonEscape(cfg.api)
              << "\",\"mode\":\"" << JsonEscape(cfg.mode)
              << "\",\"backend\":\"" << JsonEscape(cfg.backend)
              << "\",\"workload\":\"" << JsonEscape(cfg.workload)
              << "\",\"model\":\"" << JsonEscape(cfg.model_path)
              << "\",\"input_manifest\":\"" << JsonEscape(cfg.input_manifest)
              << "\",\"duration_s\":" << cfg.duration_s
              << ",\"inferences\":" << cfg.inferences
              << ",\"warmup_inferences\":" << cfg.warmup_inferences
              << ",\"timeout_s\":" << cfg.timeout_s
              << ",\"inference_timeout_ms\":" << cfg.inference_timeout_ms
              << ",\"simulated_latency_ms\":" << cfg.simulated_latency_ms
              << ",\"input_elements\":" << cfg.input_elements
              << ",\"output_elements\":" << cfg.output_elements
              << ",\"seed\":" << cfg.seed
              << ",\"verify_mode\":\"" << JsonEscape(cfg.verify_mode)
              << "\",\"verify_interval\":" << cfg.verify_interval
              << ",\"heartbeat_interval_s\":" << cfg.heartbeat_interval_s
              << ",\"summary_only\":" << (cfg.summary_only ? "true" : "false")
              << ",\"per_inference_log\":" << (cfg.per_inference_log ? "true" : "false")
              << "}\n";
}

void PrintHelp() {
    std::cout << R"(npu-avs-workload

Usage: npu-avs-workload [options]

Core:
  --profile <null|reference>
  --backend <null|reference_cpu>
  --config <path>                 Flat JSON configuration
  --workload <name>
  --model <path>
  --input-manifest <path>

Runtime:
  --duration <sec>
  --inferences <count>            Stop after completed inferences
  --warmup-inferences <count>
  --timeout <sec>
  --inference-timeout-ms <ms>
  --simulated-latency-ms <ms>     Test-only backend delay
  --input-elements <count>
  --output-elements <count>
  --seed <integer>

Verification and output:
  --verify-mode <none|checksum|crc>
  --verify-interval <count>
  --golden-checksum <hex>
  --generate-golden[ <bool>]
  --heartbeat-interval <sec>
  --output <path>
  --summary-only[ <bool>]
  --per-inference-log[ <bool>]

Utility: --list-profiles --dump-effective-config --help --version
)";
}

void PrintVersion() { std::cout << "npu-avs-workload 0.1.0\n"; }

} // namespace npu_avs
