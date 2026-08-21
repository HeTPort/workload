#include "gpu_avs/profile.h"

#include <iostream>

namespace gpu_avs {

std::vector<std::string> ListProfiles() {
    return {
        "idle",
        "ui_burst",
        "game_light",
        "game_mid",
        "game_heavy",
        "alu",
        "sfu",
        "texture",
        "fill",
        "mixed",
        "burst",
        "thermal",
        "stress_extreme"
    };
}

bool ApplyProfileDefaults(const std::string& profile, WorkloadConfig& cfg) {
    cfg.profile = profile;

    if (profile == "idle") {
        cfg.mode = "offscreen";
        cfg.width = 1;
        cfg.height = 1;
        cfg.shader = "none";
        cfg.complexity = "low";
        cfg.iterations = 0;
        cfg.duty_cycle = 0.0;
        cfg.verify_mode = "none";
        cfg.duration_s = 30.0;
        cfg.warmup_s = 0.0;
        cfg.timeout_s = 60.0;
        return true;
    }

    if (profile == "ui_burst") {
        cfg.mode = "offscreen";
        cfg.width = 1920;
        cfg.height = 1080;
        cfg.shader = "mixed";
        cfg.complexity = "low";
        cfg.iterations = 64;
        cfg.duty_cycle = 0.2;
        cfg.burst_period_s = 1.0;
        cfg.burst_active_s = 0.2;
        cfg.verify_mode = "none";
        cfg.duration_s = 60.0;
        cfg.warmup_s = 5.0;
        cfg.timeout_s = 95.0;
        return true;
    }

    if (profile == "game_light") {
        cfg.mode = "offscreen";
        cfg.width = 1280;
        cfg.height = 720;
        cfg.shader = "mixed";
        cfg.complexity = "medium";
        cfg.iterations = 128;
        cfg.texture_count = 2;
        cfg.duty_cycle = 0.6;
        cfg.verify_mode = "none";
        cfg.duration_s = 60.0;
        cfg.warmup_s = 5.0;
        cfg.timeout_s = 95.0;
        return true;
    }

    if (profile == "game_mid") {
        cfg.mode = "offscreen";
        cfg.width = 1920;
        cfg.height = 1080;
        cfg.shader = "mixed";
        cfg.complexity = "medium";
        cfg.iterations = 192;
        cfg.texture_count = 4;
        cfg.duty_cycle = 1.0;
        cfg.verify_mode = "none";
        cfg.duration_s = 60.0;
        cfg.warmup_s = 5.0;
        cfg.timeout_s = 95.0;
        return true;
    }

    if (profile == "game_heavy") {
        cfg.mode = "offscreen";
        cfg.width = 2560;
        cfg.height = 1440;
        cfg.shader = "mixed";
        cfg.complexity = "high";
        cfg.iterations = 384;
        cfg.texture_count = 6;
        cfg.duty_cycle = 1.0;
        cfg.verify_mode = "none";
        cfg.duration_s = 60.0;
        cfg.warmup_s = 5.0;
        cfg.timeout_s = 120.0;
        return true;
    }

    if (profile == "alu") {
        cfg.mode = "compute";
        cfg.width = 1920;
        cfg.height = 1080;
        cfg.shader = "alu";
        cfg.complexity = "high";
        cfg.iterations = 512;
        cfg.texture_count = 1;
        cfg.duty_cycle = 1.0;
        cfg.verify_mode = "none";
        cfg.duration_s = 60.0;
        cfg.warmup_s = 5.0;
        cfg.timeout_s = 120.0;
        return true;
    }

    if (profile == "sfu") {
        cfg.mode = "compute";
        cfg.width = 1920;
        cfg.height = 1080;
        cfg.shader = "sfu";
        cfg.complexity = "high";
        cfg.iterations = 512;
        cfg.texture_count = 1;
        cfg.duty_cycle = 1.0;
        cfg.verify_mode = "none";
        cfg.duration_s = 60.0;
        cfg.warmup_s = 5.0;
        cfg.timeout_s = 120.0;
        return true;
    }

    if (profile == "texture") {
        cfg.mode = "offscreen";
        cfg.width = 1920;
        cfg.height = 1080;
        cfg.shader = "texture";
        cfg.complexity = "high";
        cfg.iterations = 128;
        cfg.texture_count = 8;
        cfg.texture_size = "4096x4096";
        cfg.duty_cycle = 1.0;
        cfg.verify_mode = "none";
        cfg.duration_s = 60.0;
        cfg.warmup_s = 5.0;
        cfg.timeout_s = 120.0;
        return true;
    }

    if (profile == "fill") {
        cfg.mode = "offscreen";
        cfg.width = 3840;
        cfg.height = 2160;
        cfg.shader = "fill";
        cfg.complexity = "high";
        cfg.iterations = 16;
        cfg.texture_count = 1;
        cfg.duty_cycle = 1.0;
        cfg.verify_mode = "none";
        cfg.duration_s = 60.0;
        cfg.warmup_s = 5.0;
        cfg.timeout_s = 120.0;
        return true;
    }

    if (profile == "mixed") {
        cfg.mode = "offscreen";
        cfg.width = 1920;
        cfg.height = 1080;
        cfg.shader = "mixed";
        cfg.complexity = "high";
        cfg.iterations = 384;
        cfg.texture_count = 4;
        cfg.duty_cycle = 1.0;
        cfg.verify_mode = "none";
        cfg.duration_s = 60.0;
        cfg.warmup_s = 5.0;
        cfg.timeout_s = 120.0;
        return true;
    }

    if (profile == "burst") {
        cfg.mode = "offscreen";
        cfg.width = 1920;
        cfg.height = 1080;
        cfg.shader = "mixed";
        cfg.complexity = "high";
        cfg.iterations = 384;
        cfg.texture_count = 4;
        cfg.duty_cycle = 0.5;
        cfg.burst_period_s = 2.0;
        cfg.burst_active_s = 0.5;
        cfg.verify_mode = "none";
        cfg.duration_s = 120.0;
        cfg.warmup_s = 5.0;
        cfg.timeout_s = 180.0;
        return true;
    }

    if (profile == "thermal") {
        cfg.mode = "offscreen";
        cfg.width = 1920;
        cfg.height = 1080;
        cfg.shader = "mixed";
        cfg.complexity = "high";
        cfg.iterations = 512;
        cfg.texture_count = 4;
        cfg.duty_cycle = 1.0;
        cfg.verify_mode = "none";
        cfg.duration_s = 600.0;
        cfg.warmup_s = 10.0;
        cfg.timeout_s = 700.0;
        return true;
    }

    if (profile == "stress_extreme") {
        cfg.mode = "offscreen";
        cfg.width = 3840;
        cfg.height = 2160;
        cfg.shader = "mixed";
        cfg.complexity = "extreme";
        cfg.iterations = 1024;
        cfg.texture_count = 8;
        cfg.texture_size = "4096x4096";
        cfg.duty_cycle = 1.0;
        cfg.verify_mode = "none";
        cfg.duration_s = 120.0;
        cfg.warmup_s = 10.0;
        cfg.timeout_s = 240.0;
        return true;
    }

    return false;
}

void PrintProfiles() {
    for (const auto& p : ListProfiles()) {
        std::cout << p << "\n";
    }
}

} // namespace gpu_avs
