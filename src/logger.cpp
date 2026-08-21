#include "gpu_avs/logger.h"

#include <iomanip>
#include <iostream>
#include <sstream>

namespace gpu_avs {

Logger::Logger(const WorkloadConfig& cfg)
    : cfg_(cfg) {}

Logger::~Logger() {
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
}

bool Logger::Open(std::string& error) {
    if (!cfg_.output_path.empty()) {
        file_.open(cfg_.output_path);
        if (!file_) {
            error = "failed to open output file: " + cfg_.output_path;
            return false;
        }
        out_ = &file_;
    } else {
        out_ = &std::cout;
    }

    return true;
}

std::string Logger::JsonEscape(const std::string& s) {
    std::ostringstream os;

    for (char c : s) {
        switch (c) {
            case '\\': os << "\\\\"; break;
            case '"': os << "\\\""; break;
            case '\n': os << "\\n"; break;
            case '\r': os << "\\r"; break;
            case '\t': os << "\\t"; break;
            default: os << c; break;
        }
    }

    return os.str();
}

void Logger::WriteLine(const std::string& line) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (out_) {
        (*out_) << line << "\n";
        out_->flush();
    }
}

void Logger::EmitStart(const WorkloadConfig& cfg) {
    if (cfg.summary_only) {
        return;
    }

    std::ostringstream os;
    os << "{"
       << "\"type\":\"start\","
       << "\"profile\":\"" << JsonEscape(cfg.profile) << "\","
       << "\"api\":\"" << JsonEscape(cfg.api) << "\","
       << "\"mode\":\"" << JsonEscape(cfg.mode) << "\","
       << "\"width\":" << cfg.width << ","
       << "\"height\":" << cfg.height << ","
       << "\"rt_format\":\"" << JsonEscape(cfg.rt_format) << "\","
       << "\"duration_s\":" << cfg.duration_s << ","
       << "\"warmup_s\":" << cfg.warmup_s
       << "}";

    WriteLine(os.str());
}

void Logger::EmitHeartbeat(
    uint64_t timestamp_ms,
    double elapsed_s,
    const std::string& phase,
    uint64_t frame_count,
    uint64_t gpu_job_count,
    double last_frame_time_ms,
    double last_gpu_time_ms
) {
    last_heartbeat_time_ms_ = timestamp_ms;

    if (cfg_.summary_only) {
        return;
    }

    std::ostringstream os;
    os << "{"
       << "\"type\":\"heartbeat\","
       << "\"timestamp_ms\":" << timestamp_ms << ","
       << "\"elapsed_s\":" << elapsed_s << ","
       << "\"phase\":\"" << JsonEscape(phase) << "\","
       << "\"frame_count\":" << frame_count << ","
       << "\"gpu_job_count\":" << gpu_job_count << ","
       << "\"last_frame_time_ms\":" << last_frame_time_ms << ","
       << "\"last_gpu_time_ms\":" << last_gpu_time_ms
       << "}";

    WriteLine(os.str());
}

void Logger::EmitVerify(
    uint64_t frame,
    const std::string& verify_mode,
    const std::string& checksum,
    const std::string& golden_checksum,
    bool pass,
    uint64_t mismatch_count,
    uint64_t pixel_diff_count,
    uint64_t compute_mismatch_count,
    const std::string& message
) {
    if (cfg_.summary_only) {
        return;
    }

    std::ostringstream os;
    os << "{"
       << "\"type\":\"verify\","
       << "\"frame\":" << frame << ","
       << "\"verify_mode\":\"" << JsonEscape(verify_mode) << "\","
       << "\"checksum\":\"" << JsonEscape(checksum) << "\","
       << "\"golden_checksum\":\"" << JsonEscape(golden_checksum) << "\","
       << "\"result\":\"" << (pass ? "PASS" : "FAIL") << "\","
       << "\"mismatch_count\":" << mismatch_count << ","
       << "\"pixel_diff_count\":" << pixel_diff_count << ","
       << "\"compute_mismatch_count\":" << compute_mismatch_count << ","
       << "\"message\":\"" << JsonEscape(message) << "\""
       << "}";

    WriteLine(os.str());
}

void Logger::EmitGolden(
    const WorkloadConfig& cfg,
    const std::string& checksum,
    const std::string& golden_file
) {
    std::ostringstream os;
    os << "{"
       << "\"type\":\"golden\","
       << "\"profile\":\"" << JsonEscape(cfg.profile) << "\","
       << "\"api\":\"" << JsonEscape(cfg.api) << "\","
       << "\"mode\":\"" << JsonEscape(cfg.mode) << "\","
       << "\"width\":" << cfg.width << ","
       << "\"height\":" << cfg.height << ","
       << "\"rt_format\":\"" << JsonEscape(cfg.rt_format) << "\","
       << "\"verify_mode\":\"" << JsonEscape(cfg.verify_mode) << "\","
       << "\"checksum\":\"" << JsonEscape(checksum) << "\","
       << "\"golden_file\":\"" << JsonEscape(golden_file) << "\""
       << "}";

    WriteLine(os.str());
}

void Logger::EmitError(
    uint64_t timestamp_ms,
    uint64_t frame,
    const std::string& error_type,
    const std::string& api_error_code,
    const std::string& message
) {
    std::ostringstream os;
    os << "{"
       << "\"type\":\"error\","
       << "\"timestamp_ms\":" << timestamp_ms << ","
       << "\"frame\":" << frame << ","
       << "\"error_type\":\"" << JsonEscape(error_type) << "\","
       << "\"api_error_code\":\"" << JsonEscape(api_error_code) << "\","
       << "\"message\":\"" << JsonEscape(message) << "\""
       << "}";

    WriteLine(os.str());
}

void Logger::EmitSummary(const SummaryData& s) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(4);

    os << "{"
       << "\"type\":\"summary\","
       << "\"result\":\"" << ResultToString(s.result) << "\","
       << "\"exit_code\":" << s.exit_code << ","

       << "\"profile\":\"" << JsonEscape(s.config.profile) << "\","
       << "\"api\":\"" << JsonEscape(s.config.api) << "\","
       << "\"mode\":\"" << JsonEscape(s.config.mode) << "\","
       << "\"width\":" << s.config.width << ","
       << "\"height\":" << s.config.height << ","
       << "\"rt_format\":\"" << JsonEscape(s.config.rt_format) << "\","

       << "\"warmup_s\":" << s.actual_warmup_s << ","
       << "\"duration_s\":" << s.actual_duration_s << ","
       << "\"frame_count\":" << s.frame_count << ","

       << "\"fps_avg\":" << s.fps_avg << ","
       << "\"fps_min\":" << s.fps_min << ","
       << "\"fps_max\":" << s.fps_max << ","

       << "\"frame_time_avg_ms\":" << s.frame_time.avg << ","
       << "\"frame_time_min_ms\":" << s.frame_time.min << ","
       << "\"frame_time_max_ms\":" << s.frame_time.max << ","
       << "\"frame_time_p95_ms\":" << s.frame_time.p95 << ","
       << "\"frame_time_p99_ms\":" << s.frame_time.p99 << ","

       << "\"gpu_timestamp_valid\":" << (s.gpu_timestamp_valid ? "true" : "false") << ","
       << "\"gpu_job_time_avg_ms\":" << s.gpu_job_time.avg << ","
       << "\"gpu_job_time_min_ms\":" << s.gpu_job_time.min << ","
       << "\"gpu_job_time_max_ms\":" << s.gpu_job_time.max << ","
       << "\"gpu_job_time_p95_ms\":" << s.gpu_job_time.p95 << ","
       << "\"gpu_job_time_p99_ms\":" << s.gpu_job_time.p99 << ","

       << "\"verify_pass\":" << (s.verify_pass ? "true" : "false") << ","
       << "\"verify_mode\":\"" << JsonEscape(s.verify_mode) << "\","
       << "\"verify_fail_count\":" << s.verify_fail_count << ","
       << "\"first_fail_frame\":" << s.first_fail_frame << ","
       << "\"checksum\":\"" << JsonEscape(s.checksum) << "\","
       << "\"golden_checksum\":\"" << JsonEscape(s.golden_checksum) << "\","
       << "\"pixel_diff_count\":" << s.pixel_diff_count << ","
       << "\"compute_mismatch_count\":" << s.compute_mismatch_count << ","

       << "\"timeout_count\":" << s.timeout_count << ","
       << "\"api_error_count\":" << s.api_error_count << ","
       << "\"device_lost_count\":" << s.device_lost_count << ","
       << "\"allocation_fail_count\":" << s.allocation_fail_count << ","

       << "\"heartbeat_last_time_ms\":" << s.heartbeat_last_time_ms << ","
       << "\"last_error\":\"" << JsonEscape(s.last_error) << "\","
       << "\"last_error_code\":\"" << JsonEscape(s.last_error_code) << "\""
       << "}";

    WriteLine(os.str());
}

uint64_t Logger::LastHeartbeatTimeMs() const {
    return last_heartbeat_time_ms_;
}

} // namespace gpu_avs
