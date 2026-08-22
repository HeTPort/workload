#pragma once

#include "cpu_avs/logger.h"

#include <cstdint>
#include <string>

namespace cpu_avs {

class Heartbeat {
public:
    explicit Heartbeat(double interval_s);

    bool MaybeEmit(Logger& logger, uint64_t timestamp_ms, double elapsed_s,
                   const std::string& phase, uint64_t batch_count,
                   uint64_t operation_count, double last_batch_time_ms,
                   uint64_t checksum_mismatch_count, uint64_t worker_error_count,
                   double& emitted_operations_per_sec);

    double MaxGapMs() const;

private:
    double interval_s_ = 1.0;
    double last_emit_elapsed_s_ = -1.0;
    uint64_t last_batch_count_ = 0;
    uint64_t last_operation_count_ = 0;
    double max_gap_ms_ = 0.0;
};

} // namespace cpu_avs
