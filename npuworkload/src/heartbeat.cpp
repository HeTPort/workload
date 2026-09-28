#include "npu_avs/heartbeat.h"

#include <algorithm>

namespace npu_avs {

Heartbeat::Heartbeat(double interval_s) : interval_s_(interval_s) {}

bool Heartbeat::MaybeEmit(Logger& logger, uint64_t timestamp_ms, double elapsed_s,
                          const std::string& phase, uint64_t inference_count,
                          uint64_t operation_count, double last_host_time_ms,
                          double last_device_time_ms, uint64_t verify_fail_count,
                          uint64_t backend_error_count, double& emitted_inferences_per_sec) {
    emitted_inferences_per_sec = 0.0;
    if (interval_s_ <= 0.0) return false;
    if (last_emit_elapsed_s_ >= 0.0 && elapsed_s - last_emit_elapsed_s_ < interval_s_) return false;

    double interval = elapsed_s;
    if (last_emit_elapsed_s_ >= 0.0) {
        interval = elapsed_s - last_emit_elapsed_s_;
        max_gap_ms_ = std::max(max_gap_ms_, interval * 1000.0);
    }
    const uint64_t interval_inferences = inference_count - last_inference_count_;
    if (interval > 0.0) emitted_inferences_per_sec = interval_inferences / interval;

    HeartbeatData data;
    data.timestamp_ms = timestamp_ms;
    data.elapsed_s = elapsed_s;
    data.phase = phase;
    data.inference_count = inference_count;
    data.operation_count = operation_count;
    data.window_inferences_per_sec = emitted_inferences_per_sec;
    data.last_host_time_ms = last_host_time_ms;
    data.last_device_time_ms = last_device_time_ms;
    data.verify_fail_count = verify_fail_count;
    data.backend_error_count = backend_error_count;
    logger.EmitHeartbeat(data);

    last_emit_elapsed_s_ = elapsed_s;
    last_inference_count_ = inference_count;
    return true;
}

double Heartbeat::MaxGapMs() const { return max_gap_ms_; }

} // namespace npu_avs
