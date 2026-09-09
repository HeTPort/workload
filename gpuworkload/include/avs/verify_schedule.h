#pragma once

#include <cstdint>

namespace avs {

constexpr bool ShouldVerify(uint64_t work_unit_index, uint32_t verify_interval) {
    return work_unit_index > 0 && verify_interval > 0 &&
           work_unit_index % verify_interval == 0;
}

constexpr bool ShouldLogSuccessfulVerify(
    uint64_t successful_verify_index,
    uint32_t success_log_interval
) {
    return successful_verify_index > 0 && success_log_interval > 0 &&
           (successful_verify_index - 1) % success_log_interval == 0;
}

// Diagnostic success events must not scale without bound with throughput.
class SuccessLogRateLimit {
public:
    bool Allow(uint64_t now_ms) {
        if (emitted_ && (now_ms < last_ms_ || now_ms - last_ms_ < 1000)) return false;
        emitted_ = true;
        last_ms_ = now_ms;
        return true;
    }
private:
    bool emitted_ = false;
    uint64_t last_ms_ = 0;
};

static_assert(ShouldVerify(1, 1));
static_assert(!ShouldVerify(1, 2));
static_assert(ShouldVerify(2, 2));
static_assert(!ShouldVerify(2, 0));
static_assert(ShouldLogSuccessfulVerify(1, 60));
static_assert(!ShouldLogSuccessfulVerify(2, 60));
static_assert(ShouldLogSuccessfulVerify(61, 60));
static_assert(!ShouldLogSuccessfulVerify(1, 0));

} // namespace avs
