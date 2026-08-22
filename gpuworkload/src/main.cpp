#include "gpu_avs/config.h"
#include "gpu_avs/profile.h"
#include "gpu_avs/result.h"
#include "gpu_avs/runner.h"

#include <iostream>
#include <string>

using namespace gpu_avs;

static std::string JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);

    for (char ch : s) {
        switch (ch) {
        case '\"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            out += ch;
            break;
        }
    }

    return out;
}

static void DumpEffectiveConfig(const WorkloadConfig& cfg) {
    std::cout
        << "{"
        << "\"profile\":\"" << JsonEscape(cfg.profile) << "\","
        << "\"api\":\"" << JsonEscape(cfg.api) << "\","
        << "\"mode\":\"" << JsonEscape(cfg.mode) << "\","

        << "\"width\":" << cfg.width << ","
        << "\"height\":" << cfg.height << ","
        << "\"rt_format\":\"" << JsonEscape(cfg.rt_format) << "\","
        << "\"samples\":" << cfg.samples << ","

        << "\"duration_s\":" << cfg.duration_s << ","
        << "\"frames\":" << cfg.frames << ","
        << "\"warmup_s\":" << cfg.warmup_s << ","
        << "\"timeout_s\":" << cfg.timeout_s << ","

        << "\"shader\":\"" << JsonEscape(cfg.shader) << "\","
        << "\"shader_dir\":\"" << JsonEscape(cfg.shader_dir) << "\","
        << "\"complexity\":\"" << JsonEscape(cfg.complexity) << "\","
        << "\"iterations\":" << cfg.iterations << ","
        << "\"texture_count\":" << cfg.texture_count << ","
        << "\"texture_size\":\"" << JsonEscape(cfg.texture_size) << "\","
        << "\"duty_cycle\":" << cfg.duty_cycle << ","
        << "\"burst_period_s\":" << cfg.burst_period_s << ","
        << "\"burst_active_s\":" << cfg.burst_active_s << ","

        << "\"verify_mode\":\"" << JsonEscape(cfg.verify_mode) << "\","
        << "\"checksum_interval\":" << cfg.checksum_interval << ","
        << "\"golden_checksum\":\"" << JsonEscape(cfg.golden_checksum) << "\","
        << "\"golden_file\":\"" << JsonEscape(cfg.golden_file) << "\","
        << "\"fail_fast\":" << (cfg.fail_fast ? "true" : "false") << ","
        << "\"generate_golden\":" << (cfg.generate_golden ? "true" : "false") << ","

        << "\"gpu_timestamp\":" << (cfg.gpu_timestamp ? "true" : "false") << ","
        << "\"timestamp_scope\":\"" << JsonEscape(cfg.timestamp_scope) << "\","
        << "\"gpu_timeout_ms\":" << cfg.gpu_timeout_ms << ","

        << "\"heartbeat_interval_s\":" << cfg.heartbeat_interval_s << ","
        << "\"output_format\":\"" << JsonEscape(cfg.output_format) << "\","
        << "\"output\":\"" << JsonEscape(cfg.output_path) << "\","
        << "\"summary_only\":" << (cfg.summary_only ? "true" : "false") << ","
        << "\"per_frame_log\":" << (cfg.per_frame_log ? "true" : "false") << ","
        << "\"log_level\":\"" << JsonEscape(cfg.log_level) << "\""
        << "}\n";
}

int main(int argc, char** argv) {
    WorkloadConfig cfg;
    std::string error;

    if (!ParseCommandLine(argc, argv, cfg, error)) {
        std::cerr << "error: " << error << "\n";
        return ResultToExitCode(ResultCode::UNKNOWN_ERROR);
    }

    if (cfg.show_help) {
        PrintHelp();
        return 0;
    }

    if (cfg.show_version) {
        PrintVersion();
        return 0;
    }

    if (cfg.list_profiles) {
        PrintProfiles();
        return 0;
    }

    if (cfg.dump_effective_config) {
        DumpEffectiveConfig(cfg);
        return 0;
    }

    ResultCode result = RunWorkload(cfg);
    return ResultToExitCode(result);
}
