#pragma once

#include "gpu_avs/config.h"
#include "gpu_avs/metrics.h"
#include "gpu_avs/result.h"

#include <cstdint>
#include <fstream>
#include <mutex>
#include <ostream>
#include <string>

namespace gpu_avs {

struct SummaryData {
    ResultCode result = ResultCode::PASS;
    int exit_code = 0;

    WorkloadConfig config;

    uint64_t frame_count = 0;
    double actual_duration_s = 0.0;
    double actual_warmup_s = 0.0;

    double fps_avg = 0.0;
    double fps_min = 0.0;
    double fps_max = 0.0;

    StatSummary frame_time;

    bool gpu_timestamp_valid = false;
    StatSummary gpu_job_time;

    bool verify_pass = true;
    std::string verify_mode = "none";
    uint64_t verify_fail_count = 0;
    int64_t first_fail_frame = -1;
    std::string checksum;
    std::string golden_checksum;

    uint64_t pixel_diff_count = 0;
    uint64_t compute_mismatch_count = 0;

    uint64_t timeout_count = 0;
    uint64_t api_error_count = 0;
    uint64_t device_lost_count = 0;
    uint64_t allocation_fail_count = 0;

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

    void EmitHeartbeat(
        uint64_t timestamp_ms,
        double elapsed_s,
        const std::string& phase,
        uint64_t frame_count,
        uint64_t gpu_job_count,
        double last_frame_time_ms,
        double last_gpu_time_ms
    );

    void EmitVerify(
        uint64_t frame,
        const std::string& verify_mode,
        const std::string& checksum,
        const std::string& golden_checksum,
        bool pass,
        uint64_t mismatch_count,
        uint64_t pixel_diff_count,
        uint64_t compute_mismatch_count,
        const std::string& message
    );

    void EmitGolden(
        const WorkloadConfig& cfg,
        const std::string& checksum,
        const std::string& golden_file
    );

    void EmitError(
        uint64_t timestamp_ms,
        uint64_t frame,
        const std::string& error_type,
        const std::string& api_error_code,
        const std::string& message
    );

    void EmitSummary(const SummaryData& summary);

    uint64_t LastHeartbeatTimeMs() const;

private:
    void WriteLine(const std::string& line);
    std::string JsonEscape(const std::string& s);

private:
    WorkloadConfig cfg_;
    std::ofstream file_;
    std::ostream* out_ = nullptr;
    mutable std::mutex mutex_;
    uint64_t last_heartbeat_time_ms_ = 0;
};

} // namespace gpu_avs
