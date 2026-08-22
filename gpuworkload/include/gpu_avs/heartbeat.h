#pragma once

#include "gpu_avs/logger.h"

#include <cstdint>
#include <string>

namespace gpu_avs {

class Heartbeat {
public:
    explicit Heartbeat(double interval_s);

    void MaybeEmit(
        Logger& logger,
        double elapsed_s,
        const std::string& phase,
        uint64_t frame_count,
        uint64_t gpu_job_count,
        double last_frame_time_ms,
        double last_gpu_time_ms
    );

private:
    double interval_s_ = 1.0;
    double last_emit_elapsed_s_ = -1.0;
};

} // namespace gpu_avs
