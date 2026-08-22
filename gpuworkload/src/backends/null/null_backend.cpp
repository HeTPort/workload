#include "null_backend.h"

#include "gpu_avs/utils.h"

#include <cstdint>
#include <string>

namespace gpu_avs {

bool NullBackend::Init(const WorkloadConfig& cfg, std::string& error) {
    (void)error;
    cfg_ = cfg;
    return true;
}

bool NullBackend::CreateResources(std::string& error) {
    (void)error;
    return true;
}

SubmitStatus NullBackend::SubmitWorkload(uint64_t frame_index) {
    (void)frame_index;

    uint32_t base_ms = 4;

    if (cfg_.profile == "idle") {
        base_ms = 1;
    } else if (cfg_.complexity == "low") {
        base_ms = 3;
    } else if (cfg_.complexity == "medium") {
        base_ms = 6;
    } else if (cfg_.complexity == "high") {
        base_ms = 10;
    } else if (cfg_.complexity == "extreme") {
        base_ms = 16;
    }

    if (cfg_.duty_cycle <= 0.0) {
        SleepMs(16);
        last_gpu_time_ms_ = 0.0;
        return SubmitStatus::Ok;
    }

    const double now = NowSeconds();
    bool active = true;

    if (cfg_.burst_period_s > 0.0 &&
        cfg_.burst_active_s >= 0.0 &&
        cfg_.duty_cycle < 1.0) {
        double phase =
            now -
            static_cast<uint64_t>(now / cfg_.burst_period_s) *
            cfg_.burst_period_s;

        active = phase < cfg_.burst_active_s;
    }

    if (active) {
        SleepMs(base_ms);
        last_gpu_time_ms_ = static_cast<double>(base_ms);
    } else {
        SleepMs(1);
        last_gpu_time_ms_ = 0.2;
    }

    return SubmitStatus::Ok;
}

bool NullBackend::WaitIdleOrFrameDone(uint64_t timeout_ns, std::string& error) {
    (void)timeout_ns;
    (void)error;
    return true;
}

static uint32_t HashString(const std::string& s) {
    uint32_t h = 2166136261u;

    for (char c : s) {
        h ^= static_cast<uint8_t>(c);
        h *= 16777619u;
    }

    return h;
}

bool NullBackend::Readback(ReadbackBuffer& out, std::string& error) {
    (void)error;

    out.width = cfg_.width;
    out.height = cfg_.height;
    out.format = cfg_.rt_format;

    const size_t pixel_count =
        static_cast<size_t>(cfg_.width) *
        static_cast<size_t>(cfg_.height);

    const size_t bytes_per_pixel = 4;
    out.data.resize(pixel_count * bytes_per_pixel);

    const uint32_t seed =
        HashString(cfg_.profile) ^
        HashString(cfg_.shader) ^
        HashString(cfg_.complexity) ^
        HashString(cfg_.rt_format) ^
        cfg_.width ^
        (cfg_.height << 1) ^
        (cfg_.iterations << 2) ^
        (cfg_.texture_count << 3);

    for (uint32_t y = 0; y < cfg_.height; ++y) {
        for (uint32_t x = 0; x < cfg_.width; ++x) {
            size_t idx =
                (static_cast<size_t>(y) * cfg_.width + x) *
                bytes_per_pixel;

            uint32_t v =
                seed ^
                (x * 73856093u) ^
                (y * 19349663u);

            out.data[idx + 0] = static_cast<uint8_t>((v >> 0) & 0xFF);
            out.data[idx + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
            out.data[idx + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
            out.data[idx + 3] = 0xFF;
        }
    }

    return true;
}

bool NullBackend::SupportsGpuTimestamp() const {
    return true;
}

bool NullBackend::GetLastGpuTimeMs(double& out_ms) {
    out_ms = last_gpu_time_ms_;
    return true;
}

void NullBackend::Destroy() {}

} // namespace gpu_avs
