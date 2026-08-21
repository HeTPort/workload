#include "gpu_avs/heartbeat.h"
#include "gpu_avs/utils.h"

namespace gpu_avs {

Heartbeat::Heartbeat(double interval_s)
    : interval_s_(interval_s) {}

void Heartbeat::MaybeEmit(
    Logger& logger,
    double elapsed_s,
    const std::string& phase,
    uint64_t frame_count,
    uint64_t gpu_job_count,
    double last_frame_time_ms,
    double last_gpu_time_ms
) {
    if (interval_s_ <= 0.0) {
        return;
    }

    if (last_emit_elapsed_s_ < 0.0 ||
        elapsed_s - last_emit_elapsed_s_ >= interval_s_) {
        last_emit_elapsed_s_ = elapsed_s;

        logger.EmitHeartbeat(
            NowMs(),
            elapsed_s,
            phase,
            frame_count,
            gpu_job_count,
            last_frame_time_ms,
            last_gpu_time_ms
        );
    }
}

} // namespace gpu_avs
