#pragma once

#include "npu_avs/logger.h"

#include <cstdint>
#include <string>

namespace npu_avs {

class Heartbeat {
public:
    explicit Heartbeat(double interval_s);

    bool MaybeEmit(Logger& logger, uint64_t timestamp_ms, double elapsed_s,
                   const std::string& phase, uint64_t inference_count,
                   uint64_t operation_count, double last_host_time_ms,
                   double last_device_time_ms, uint64_t verify_fail_count,
                   uint64_t backend_error_count, double& emitted_inferences_per_sec);
    double MaxGapMs() const;

private:
    double interval_s_ = 1.0;
    double last_emit_elapsed_s_ = -1.0;
    uint64_t last_inference_count_ = 0;
    double max_gap_ms_ = 0.0;
};

} // namespace npu_avs
