#include "npu_avs/config.h"
#include "npu_avs/profile.h"
#include "npu_avs/result.h"
#include "npu_avs/runner.h"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    npu_avs::WorkloadConfig cfg;
    std::string error;
    if (!npu_avs::ParseCommandLine(argc, argv, cfg, error)) {
        std::cerr << "error: " << error << '\n';
        return npu_avs::ResultToExitCode(npu_avs::ResultCode::UNKNOWN_ERROR);
    }
    if (cfg.show_help) { npu_avs::PrintHelp(); return 0; }
    if (cfg.show_version) { npu_avs::PrintVersion(); return 0; }
    if (cfg.list_profiles) { npu_avs::PrintProfiles(); return 0; }
    if (cfg.dump_effective_config) { npu_avs::DumpEffectiveConfig(cfg); return 0; }
    return npu_avs::ResultToExitCode(npu_avs::RunWorkload(cfg));
}
