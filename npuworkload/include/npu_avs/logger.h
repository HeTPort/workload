#pragma once

#include "npu_avs/backend.h"
#include "npu_avs/config.h"
#include "npu_avs/metrics.h"
#include "npu_avs/result.h"

#include <cstdint>
#include <fstream>
#include <mutex>
#include <ostream>
#include <string>

namespace npu_avs {

struct HeartbeatData {
    uint64_t timestamp_ms = 0;
    double elapsed_s = 0.0;
    std::string phase;
    uint64_t inference_count = 0;
    uint64_t operation_count = 0;
    double window_inferences_per_sec = 0.0;
    double last_host_time_ms = 0.0;
    double last_device_time_ms = 0.0;
    uint64_t verify_fail_count = 0;
    uint64_t backend_error_count = 0;
};

struct VerifyData {
    uint64_t inference = 0;
    std::string verify_mode;
    std::string checksum;
    std::string golden_checksum;
    bool pass = true;
    uint64_t mismatch_count = 0;
    std::string message;
};

struct SummaryData {
    ResultCode result = ResultCode::PASS;
    int exit_code = 0;
    WorkloadConfig config;
    BackendExecutionInfo execution;

    uint64_t inference_count = 0;
    uint64_t operation_count = 0;
    double prepare_time_ms = 0.0;
    double first_inference_time_ms = 0.0;
    double actual_duration_s = 0.0;
    uint64_t actual_warmup_inferences = 0;
    double inferences_per_sec_avg = 0.0;
    StatSummary host_time;
    StatSummary device_time;
    StatSummary throughput;

    bool verify_pass = true;
    uint64_t verify_fail_count = 0;
    int64_t first_fail_inference = -1;
    std::string checksum;
    std::string golden_checksum;

    uint64_t timeout_count = 0;
    uint64_t api_error_count = 0;
    uint64_t device_lost_count = 0;
    uint64_t allocation_fail_count = 0;
    uint64_t backend_error_count = 0;
    double heartbeat_max_gap_ms = 0.0;
    uint64_t heartbeat_last_time_ms = 0;
    std::string last_error;
    std::string last_error_code;
};

class Logger {
public:
    explicit Logger(const WorkloadConfig& cfg);
    ~Logger();

    bool Open(std::string& error);
    void EmitStart(const WorkloadConfig& cfg);
    void EmitHeartbeat(const HeartbeatData& data);
    void EmitInference(uint64_t inference, const InferenceResult& result, double host_time_ms,
                       const std::string& checksum);
    void EmitVerify(const VerifyData& data);
    void EmitGolden(const WorkloadConfig& cfg, const std::string& checksum);
    void EmitError(uint64_t timestamp_ms, uint64_t inference, const std::string& error_type,
                   const std::string& error_code, const std::string& message);
    void EmitSummary(const SummaryData& summary);
    uint64_t LastHeartbeatTimeMs() const;

private:
    void WriteLine(const std::string& line);
    static std::string JsonEscape(const std::string& value);

    WorkloadConfig cfg_;
    std::ofstream file_;
    std::ostream* out_ = nullptr;
    mutable std::mutex mutex_;
    uint64_t last_heartbeat_time_ms_ = 0;
};

} // namespace npu_avs
