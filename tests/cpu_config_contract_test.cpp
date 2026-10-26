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

int main(int argc, char** argv) {
    assert(argc == 2);
    const std::string fixtures = std::string(argv[1]) + "/fixtures/";
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
    error.clear();
    assert(!Parse({"workload", "--verify-interval", "1", "--checksum-interval", "2"}, cfg, error));
    error.clear();
    assert(!Parse({"workload", "--verify_interval", "1", "--verify-interval", "2"}, cfg, error));
    error.clear();
    assert(!Parse({"workload", "--config", fixtures + "ambiguous-interval.json"}, cfg, error));
    error.clear();
    assert(Parse({"workload", "--config", fixtures + "legacy-interval.json", "--verify-interval", "1"}, cfg, error));
    assert(cfg.verify_interval == 1);
    error.clear();
    assert(Parse({"workload", "--config", fixtures + "canonical-interval.json", "--checksum-interval", "3"}, cfg, error));
    assert(cfg.verify_interval == 3);
    error.clear();
    assert(!Parse({"workload", "--verify-mode", "checksum", "--verify-interval", "0"}, cfg, error));
    error.clear();
    assert(Parse({"workload", "--verify-mode", "none", "--verify-interval", "0"}, cfg, error));
    error.clear();
    assert(Parse({"workload", "--capabilities"}, cfg, error));
    assert(cfg.show_capabilities);
    return 0;
}
