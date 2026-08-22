#include "cpu_avs/heartbeat.h"

#include <algorithm>

namespace cpu_avs {

Heartbeat::Heartbeat(double interval_s)
    : interval_s_(interval_s) {}

bool Heartbeat::MaybeEmit(Logger& logger, uint64_t timestamp_ms, double elapsed_s,
                          const std::string& phase, uint64_t batch_count,
                          uint64_t operation_count, double last_batch_time_ms,
                          uint64_t checksum_mismatch_count, uint64_t worker_error_count,
                          double& emitted_operations_per_sec) {
    emitted_operations_per_sec = 0.0;
    if (interval_s_ <= 0.0) {
        return false;
    }
    if (last_emit_elapsed_s_ >= 0.0 && elapsed_s - last_emit_elapsed_s_ < interval_s_) {
        return false;
    }

    double interval = elapsed_s;
    if (last_emit_elapsed_s_ >= 0.0) {
        interval = elapsed_s - last_emit_elapsed_s_;
        max_gap_ms_ = std::max(max_gap_ms_, interval * 1000.0);
    }

    const uint64_t interval_operations = operation_count - last_operation_count_;
    const uint64_t interval_batches = batch_count - last_batch_count_;
    if (interval > 0.0) {
        emitted_operations_per_sec = static_cast<double>(interval_operations) / interval;
    }

    HeartbeatData data;
    data.timestamp_ms = timestamp_ms;
    data.elapsed_s = elapsed_s;
    data.phase = phase;
    data.batch_count = batch_count;
    data.operation_count = operation_count;
    data.window_operations_per_sec = emitted_operations_per_sec;
    data.window_batches_per_sec = interval > 0.0
        ? static_cast<double>(interval_batches) / interval
        : 0.0;
    data.last_batch_time_ms = last_batch_time_ms;
    data.checksum_mismatch_count = checksum_mismatch_count;
    data.worker_error_count = worker_error_count;
    logger.EmitHeartbeat(data);

    last_emit_elapsed_s_ = elapsed_s;
    last_batch_count_ = batch_count;
    last_operation_count_ = operation_count;
    return true;
}

double Heartbeat::MaxGapMs() const {
    return max_gap_ms_;
}

} // namespace cpu_avs
