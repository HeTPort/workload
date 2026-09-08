#include "cpu_avs/config.h"

#include <cassert>
#include <string>
#include <vector>

namespace {

bool Parse(
    const std::vector<std::string>& values,
    cpu_avs::WorkloadConfig& cfg,
    std::string& error
) {
    std::vector<std::string> storage = values;
    std::vector<char*> argv;
    for (std::string& value : storage) {
        argv.push_back(value.data());
    }
    return cpu_avs::ParseCommandLine(static_cast<int>(argv.size()), argv.data(), cfg, error);
}

} // namespace

int main() {
    cpu_avs::WorkloadConfig cfg;
    std::string error;
    const std::vector<std::string> args = {
        "cpu-avs-workload",
        "--profile", "mixed",
        "--duration", "1",
        "--verify-mode", "checksum",
        "--verify-interval", "2",
        "--success-log-interval", "3"
    };
    assert(Parse(args, cfg, error));
    assert(error.empty());
    assert(cfg.verify_interval == 2);
    assert(cfg.success_log_interval == 3);

    cpu_avs::WorkloadConfig legacy;
    const std::vector<std::string> legacy_args = {
        "cpu-avs-workload", "--profile", "mixed", "--checksum-interval", "7"
    };
    assert(Parse(legacy_args, legacy, error));
    assert(legacy.verify_interval == 7);
    return 0;
}
