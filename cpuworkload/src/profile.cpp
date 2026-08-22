#include "cpu_avs/profile.h"

#include <iostream>

namespace cpu_avs {

std::vector<std::string> ListProfiles() {
    return {"idle", "integer", "floating_point", "matrix", "memory", "mixed",
            "burst", "thermal", "stress_extreme"};
}

bool ApplyProfileDefaults(const std::string& profile, WorkloadConfig& cfg) {
    cfg = WorkloadConfig{};
    cfg.profile = profile;

    if (profile == "idle") {
        cfg.backend = "null";
        cfg.complexity = "low";
        cfg.duration_s = 30.0;
        cfg.warmup_s = 0.0;
        cfg.timeout_s = 60.0;
        cfg.verify_mode = "none";
        return true;
    }
    if (profile == "integer") {
        cfg.backend = "integer";
        cfg.iterations = 100000;
        cfg.working_set_kb = 64;
        return true;
    }
    if (profile == "floating_point") {
        cfg.backend = "floating_point";
        cfg.iterations = 100000;
        cfg.working_set_kb = 64;
        return true;
    }
    if (profile == "matrix") {
        cfg.backend = "matrix";
        cfg.iterations = 20000;
        cfg.working_set_kb = 96;
        return true;
    }
    if (profile == "memory") {
        cfg.backend = "memory";
        cfg.iterations = 500000;
        cfg.working_set_kb = 1024;
        return true;
    }
    if (profile == "mixed") {
        cfg.backend = "mixed";
        cfg.iterations = 100000;
        cfg.working_set_kb = 256;
        return true;
    }
    if (profile == "burst") {
        cfg.backend = "mixed";
        cfg.iterations = 100000;
        cfg.working_set_kb = 256;
        cfg.duty_cycle = 0.5;
        cfg.burst_period_s = 2.0;
        cfg.burst_active_s = 1.0;
        cfg.duration_s = 120.0;
        cfg.timeout_s = 160.0;
        return true;
    }
    if (profile == "thermal") {
        cfg.backend = "mixed";
        cfg.iterations = 200000;
        cfg.working_set_kb = 512;
        cfg.duration_s = 600.0;
        cfg.warmup_s = 10.0;
        cfg.timeout_s = 660.0;
        return true;
    }
    if (profile == "stress_extreme") {
        cfg.backend = "mixed";
        cfg.complexity = "extreme";
        cfg.iterations = 500000;
        cfg.working_set_kb = 2048;
        cfg.duration_s = 120.0;
        cfg.warmup_s = 10.0;
        cfg.timeout_s = 180.0;
        return true;
    }
    return false;
}

void PrintProfiles() {
    for (const std::string& profile : ListProfiles()) {
        std::cout << profile << '\n';
    }
}

} // namespace cpu_avs
