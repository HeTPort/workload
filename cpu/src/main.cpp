#include "cpu_avs/config.h"
#include "cpu_avs/profile.h"
#include "cpu_avs/result.h"
#include "cpu_avs/runner.h"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    cpu_avs::WorkloadConfig cfg;
    std::string error;
    if (!cpu_avs::ParseCommandLine(argc, argv, cfg, error)) {
        std::cerr << "error: " << error << '\n';
        return cpu_avs::ResultToExitCode(cpu_avs::ResultCode::UNKNOWN_ERROR);
    }
    if (cfg.show_help) {
        cpu_avs::PrintHelp();
        return 0;
    }
    if (cfg.show_version) {
        cpu_avs::PrintVersion();
        return 0;
    }
    if (cfg.list_profiles) {
        cpu_avs::PrintProfiles();
        return 0;
    }
    if (cfg.dump_effective_config) {
        cpu_avs::DumpEffectiveConfig(cfg);
        return 0;
    }
    return cpu_avs::ResultToExitCode(cpu_avs::RunWorkload(cfg));
}
