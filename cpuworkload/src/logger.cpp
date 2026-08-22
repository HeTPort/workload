#include "cpu_avs/logger.h"

#include <iomanip>
#include <iostream>
#include <sstream>

namespace cpu_avs {
namespace {

void AppendStats(std::ostringstream& os, const char* prefix, const StatSummary& stats) {
    os << "\"" << prefix << "_count\":" << stats.count << ','
       << "\"" << prefix << "_avg\":" << stats.avg << ','
       << "\"" << prefix << "_min\":" << stats.min << ','
       << "\"" << prefix << "_max\":" << stats.max << ','
       << "\"" << prefix << "_p05\":" << stats.p05 << ','
       << "\"" << prefix << "_p50\":" << stats.p50 << ','
       << "\"" << prefix << "_p95\":" << stats.p95 << ','
       << "\"" << prefix << "_p99\":" << stats.p99 << ','
       << "\"" << prefix << "_stddev\":" << stats.stddev << ','
       << "\"" << prefix << "_cv_pct\":" << stats.cv_pct << ',';
}

} // namespace

Logger::Logger(const WorkloadConfig& cfg)
    : cfg_(cfg) {}

Logger::~Logger() {
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
}

bool Logger::Open(std::string& error) {
    if (cfg_.output_path.empty()) {
        out_ = &std::cout;
        return true;
    }
    file_.open(cfg_.output_path);
    if (!file_) {
        error = "failed to open output file: " + cfg_.output_path;
        return false;
    }
    out_ = &file_;
    return true;
}

std::string Logger::JsonEscape(const std::string& value) {
    std::ostringstream os;
    for (char c : value) {
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
        (*out_) << line << '\n';
        out_->flush();
    }
}

void Logger::EmitStart(const WorkloadConfig& cfg) {
    if (cfg.summary_only) return;
    std::ostringstream os;
    os << "{"
       << "\"type\":\"start\","
       << "\"schema_version\":1,"
       << "\"workload\":\"cpu\","
       << "\"profile\":\"" << JsonEscape(cfg.profile) << "\","
       << "\"api\":\"" << JsonEscape(cfg.api) << "\","
       << "\"mode\":\"" << JsonEscape(cfg.mode) << "\","
       << "\"backend\":\"" << JsonEscape(cfg.backend) << "\","
       << "\"duration_s\":" << cfg.duration_s << ','
       << "\"warmup_s\":" << cfg.warmup_s << ','
       << "\"iterations\":" << cfg.iterations << ','
       << "\"threads\":" << cfg.threads << ','
       << "\"working_set_kb\":" << cfg.working_set_kb << ','
       << "\"seed\":" << cfg.seed
       << "}";
    WriteLine(os.str());
}

void Logger::EmitHeartbeat(const HeartbeatData& data) {
    last_heartbeat_time_ms_ = data.timestamp_ms;
    if (cfg_.summary_only) return;
    std::ostringstream os;
    os << std::fixed << std::setprecision(4)
       << "{"
       << "\"type\":\"heartbeat\","
       << "\"timestamp_ms\":" << data.timestamp_ms << ','
       << "\"elapsed_s\":" << data.elapsed_s << ','
       << "\"phase\":\"" << JsonEscape(data.phase) << "\","
       << "\"work_unit_count\":" << data.batch_count << ','
       << "\"batch_count\":" << data.batch_count << ','
       << "\"operation_count\":" << data.operation_count << ','
       << "\"window_operations_per_sec\":" << data.window_operations_per_sec << ','
       << "\"window_batches_per_sec\":" << data.window_batches_per_sec << ','
       << "\"last_batch_time_ms\":" << data.last_batch_time_ms << ','
       << "\"checksum_mismatch_count\":" << data.checksum_mismatch_count << ','
       << "\"worker_error_count\":" << data.worker_error_count << ','
       << "\"frame_count\":" << data.batch_count << ','
       << "\"gpu_job_count\":0,"
       << "\"last_frame_time_ms\":" << data.last_batch_time_ms << ','
       << "\"last_gpu_time_ms\":0.0"
       << "}";
    WriteLine(os.str());
}

void Logger::EmitBatch(uint64_t batch, uint64_t operations, double time_ms,
                       const std::string& checksum) {
    if (cfg_.summary_only || !cfg_.per_batch_log) return;
    std::ostringstream os;
    os << std::fixed << std::setprecision(4)
       << "{\"type\":\"batch\","
       << "\"batch\":" << batch << ','
       << "\"frame\":" << batch << ','
       << "\"operation_count\":" << operations << ','
       << "\"batch_time_ms\":" << time_ms << ','
       << "\"operations_per_sec\":" << (time_ms > 0.0 ? operations * 1000.0 / time_ms : 0.0) << ','
       << "\"checksum\":\"" << JsonEscape(checksum) << "\"}";
    WriteLine(os.str());
}

void Logger::EmitVerify(const VerifyData& data) {
    if (cfg_.summary_only) return;
    std::ostringstream os;
    os << "{\"type\":\"verify\","
       << "\"batch\":" << data.batch << ','
       << "\"frame\":" << data.batch << ','
       << "\"verify_mode\":\"" << JsonEscape(data.verify_mode) << "\","
       << "\"checksum\":\"" << JsonEscape(data.checksum) << "\","
       << "\"golden_checksum\":\"" << JsonEscape(data.golden_checksum) << "\","
       << "\"result\":\"" << (data.pass ? "PASS" : "FAIL") << "\","
       << "\"mismatch_count\":" << data.mismatch_count << ','
       << "\"pixel_diff_count\":0,"
       << "\"compute_mismatch_count\":" << data.mismatch_count << ','
       << "\"message\":\"" << JsonEscape(data.message) << "\"}";
    WriteLine(os.str());
}

void Logger::EmitGolden(const WorkloadConfig& cfg, const std::string& checksum) {
    std::ostringstream os;
    os << "{\"type\":\"golden\","
       << "\"workload\":\"cpu\","
       << "\"profile\":\"" << JsonEscape(cfg.profile) << "\","
       << "\"api\":\"" << JsonEscape(cfg.api) << "\","
       << "\"mode\":\"" << JsonEscape(cfg.mode) << "\","
       << "\"backend\":\"" << JsonEscape(cfg.backend) << "\","
       << "\"verify_mode\":\"" << JsonEscape(cfg.verify_mode) << "\","
       << "\"checksum\":\"" << JsonEscape(checksum) << "\"}";
    WriteLine(os.str());
}

void Logger::EmitError(uint64_t timestamp_ms, uint64_t batch, const std::string& error_type,
                       const std::string& error_code, const std::string& message) {
    std::ostringstream os;
    os << "{\"type\":\"error\","
       << "\"timestamp_ms\":" << timestamp_ms << ','
       << "\"batch\":" << batch << ','
       << "\"frame\":" << batch << ','
       << "\"error_type\":\"" << JsonEscape(error_type) << "\","
       << "\"api_error_code\":\"" << JsonEscape(error_code) << "\","
       << "\"error_code\":\"" << JsonEscape(error_code) << "\","
       << "\"message\":\"" << JsonEscape(message) << "\"}";
    WriteLine(os.str());
}

void Logger::EmitSummary(const SummaryData& summary) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(4)
       << "{\"type\":\"summary\","
       << "\"schema_version\":1,"
       << "\"workload\":\"cpu\","
       << "\"result\":\"" << ResultToString(summary.result) << "\","
       << "\"exit_code\":" << summary.exit_code << ','
       << "\"profile\":\"" << JsonEscape(summary.config.profile) << "\","
       << "\"api\":\"" << JsonEscape(summary.config.api) << "\","
       << "\"mode\":\"" << JsonEscape(summary.config.mode) << "\","
       << "\"backend\":\"" << JsonEscape(summary.config.backend) << "\","
       << "\"threads\":" << summary.config.threads << ','
       << "\"iterations_per_batch\":" << summary.config.iterations << ','
       << "\"working_set_kb\":" << summary.config.working_set_kb << ','
       << "\"warmup_s\":" << summary.actual_warmup_s << ','
       << "\"duration_s\":" << summary.actual_duration_s << ','
       << "\"work_unit_count\":" << summary.batch_count << ','
       << "\"batch_count\":" << summary.batch_count << ','
       << "\"operation_count\":" << summary.operation_count << ','
       << "\"operations_per_sec_avg\":" << summary.operations_per_sec_avg << ','
       << "\"batches_per_sec_avg\":" << summary.batches_per_sec_avg << ',';

    AppendStats(os, "batch_time_ms", summary.batch_time);
    AppendStats(os, "window_operations_per_sec", summary.throughput);

    os << "\"verify_pass\":" << (summary.verify_pass ? "true" : "false") << ','
       << "\"verify_mode\":\"" << JsonEscape(summary.config.verify_mode) << "\","
       << "\"verify_fail_count\":" << summary.verify_fail_count << ','
       << "\"first_fail_batch\":" << summary.first_fail_batch << ','
       << "\"first_fail_frame\":" << summary.first_fail_batch << ','
       << "\"checksum\":\"" << JsonEscape(summary.checksum) << "\","
       << "\"golden_checksum\":\"" << JsonEscape(summary.golden_checksum) << "\","
       << "\"compute_mismatch_count\":" << summary.verify_fail_count << ','
       << "\"pixel_diff_count\":0,"
       << "\"operations_per_sec_min\":" << summary.throughput.min << ','
       << "\"operations_per_sec_max\":" << summary.throughput.max << ','
       << "\"operations_per_sec_p05\":" << summary.throughput.p05 << ','
       << "\"operations_per_sec_p50\":" << summary.throughput.p50 << ','
       << "\"operations_per_sec_p95\":" << summary.throughput.p95 << ','
       << "\"operations_per_sec_p99\":" << summary.throughput.p99 << ','
       << "\"operations_per_sec_stddev\":" << summary.throughput.stddev << ','
       << "\"operations_per_sec_cv_pct\":" << summary.throughput.cv_pct << ','
       << "\"performance_stable\":" << (summary.performance_stable ? "true" : "false") << ','
       << "\"performance_warning\":\"" << JsonEscape(summary.performance_warning) << "\","
       << "\"timeout_count\":" << summary.timeout_count << ','
       << "\"api_error_count\":" << summary.api_error_count << ','
       << "\"device_lost_count\":0,"
       << "\"allocation_fail_count\":" << summary.allocation_fail_count << ','
       << "\"worker_error_count\":" << summary.worker_error_count << ','
       << "\"heartbeat_max_gap_ms\":" << summary.heartbeat_max_gap_ms << ','
       << "\"heartbeat_last_time_ms\":" << summary.heartbeat_last_time_ms << ','

       // Legacy GPU-shaped fields retained for parsers shared with the GPU tool.
       << "\"frame_count\":" << summary.batch_count << ','
       << "\"fps_avg\":" << summary.batches_per_sec_avg << ','
       << "\"fps_min\":" << summary.batches_per_sec_avg << ','
       << "\"fps_max\":" << summary.batches_per_sec_avg << ','
       << "\"frame_time_avg_ms\":" << summary.batch_time.avg << ','
       << "\"frame_time_min_ms\":" << summary.batch_time.min << ','
       << "\"frame_time_max_ms\":" << summary.batch_time.max << ','
       << "\"frame_time_p95_ms\":" << summary.batch_time.p95 << ','
       << "\"frame_time_p99_ms\":" << summary.batch_time.p99 << ','
       << "\"gpu_timestamp_valid\":false,"
       << "\"gpu_job_time_avg_ms\":0.0,"
       << "\"gpu_job_time_min_ms\":0.0,"
       << "\"gpu_job_time_max_ms\":0.0,"
       << "\"gpu_job_time_p95_ms\":0.0,"
       << "\"gpu_job_time_p99_ms\":0.0,"
       << "\"last_error\":\"" << JsonEscape(summary.last_error) << "\","
       << "\"last_error_code\":\"" << JsonEscape(summary.last_error_code) << "\"}";
    WriteLine(os.str());
}

uint64_t Logger::LastHeartbeatTimeMs() const {
    return last_heartbeat_time_ms_;
}

} // namespace cpu_avs
