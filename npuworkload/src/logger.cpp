#include "npu_avs/logger.h"

#include <iomanip>
#include <iostream>
#include <sstream>

namespace npu_avs {
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

Logger::Logger(const WorkloadConfig& cfg) : cfg_(cfg) {}

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
    os << "{\"type\":\"start\",\"schema_version\":1,\"workload\":\"npu\""
       << ",\"profile\":\"" << JsonEscape(cfg.profile) << "\""
       << ",\"api\":\"" << JsonEscape(cfg.api) << "\""
       << ",\"mode\":\"" << JsonEscape(cfg.mode) << "\""
       << ",\"backend\":\"" << JsonEscape(cfg.backend) << "\""
       << ",\"workload_name\":\"" << JsonEscape(cfg.workload) << "\""
       << ",\"model\":\"" << JsonEscape(cfg.model_path) << "\""
       << ",\"input_manifest\":\"" << JsonEscape(cfg.input_manifest) << "\""
       << ",\"duration_s\":" << cfg.duration_s
       << ",\"warmup_inferences\":" << cfg.warmup_inferences
       << ",\"input_elements\":" << cfg.input_elements
       << ",\"output_elements\":" << cfg.output_elements
       << ",\"seed\":" << cfg.seed << '}';
    WriteLine(os.str());
}

void Logger::EmitHeartbeat(const HeartbeatData& data) {
    last_heartbeat_time_ms_ = data.timestamp_ms;
    if (cfg_.summary_only) return;
    std::ostringstream os;
    os << std::fixed << std::setprecision(4)
       << "{\"type\":\"heartbeat\",\"timestamp_ms\":" << data.timestamp_ms
       << ",\"elapsed_s\":" << data.elapsed_s
       << ",\"phase\":\"" << JsonEscape(data.phase) << "\""
       << ",\"work_unit_count\":" << data.inference_count
       << ",\"inference_count\":" << data.inference_count
       << ",\"operation_count\":" << data.operation_count
       << ",\"window_inferences_per_sec\":" << data.window_inferences_per_sec
       << ",\"last_host_inference_time_ms\":" << data.last_host_time_ms
       << ",\"last_device_inference_time_ms\":" << data.last_device_time_ms
       << ",\"verify_fail_count\":" << data.verify_fail_count
       << ",\"backend_error_count\":" << data.backend_error_count << '}';
    WriteLine(os.str());
}

void Logger::EmitInference(uint64_t inference, const InferenceResult& result,
                           double host_time_ms, const std::string& checksum) {
    if (cfg_.summary_only || !cfg_.per_inference_log) return;
    std::ostringstream os;
    os << std::fixed << std::setprecision(4)
       << "{\"type\":\"inference\",\"inference\":" << inference
       << ",\"operation_count\":" << result.operation_count
       << ",\"host_time_ms\":" << host_time_ms
       << ",\"device_time_valid\":" << (result.device_time_valid ? "true" : "false")
       << ",\"device_time_ms\":" << result.device_time_ms
       << ",\"checksum\":\"" << JsonEscape(checksum) << "\"}";
    WriteLine(os.str());
}

void Logger::EmitVerify(const VerifyData& data) {
    if (cfg_.summary_only) return;
    std::ostringstream os;
    os << "{\"type\":\"verify\",\"inference\":" << data.inference
       << ",\"verify_mode\":\"" << JsonEscape(data.verify_mode) << "\""
       << ",\"checksum\":\"" << JsonEscape(data.checksum) << "\""
       << ",\"golden_checksum\":\"" << JsonEscape(data.golden_checksum) << "\""
       << ",\"result\":\"" << (data.pass ? "PASS" : "FAIL") << "\""
       << ",\"mismatch_count\":" << data.mismatch_count
       << ",\"message\":\"" << JsonEscape(data.message) << "\"}";
    WriteLine(os.str());
}

void Logger::EmitGolden(const WorkloadConfig& cfg, const std::string& checksum) {
    std::ostringstream os;
    os << "{\"type\":\"golden\",\"workload\":\"npu\""
       << ",\"profile\":\"" << JsonEscape(cfg.profile) << "\""
       << ",\"backend\":\"" << JsonEscape(cfg.backend) << "\""
       << ",\"workload_name\":\"" << JsonEscape(cfg.workload) << "\""
       << ",\"verify_mode\":\"" << JsonEscape(cfg.verify_mode) << "\""
       << ",\"checksum\":\"" << JsonEscape(checksum) << "\"}";
    WriteLine(os.str());
}

void Logger::EmitError(uint64_t timestamp_ms, uint64_t inference,
                       const std::string& error_type, const std::string& error_code,
                       const std::string& message) {
    std::ostringstream os;
    os << "{\"type\":\"error\",\"timestamp_ms\":" << timestamp_ms
       << ",\"inference\":" << inference
       << ",\"error_type\":\"" << JsonEscape(error_type) << "\""
       << ",\"api_error_code\":\"" << JsonEscape(error_code) << "\""
       << ",\"error_code\":\"" << JsonEscape(error_code) << "\""
       << ",\"message\":\"" << JsonEscape(message) << "\"}";
    WriteLine(os.str());
}

void Logger::EmitSummary(const SummaryData& summary) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(4)
       << "{\"type\":\"summary\",\"schema_version\":1,\"workload\":\"npu\""
       << ",\"result\":\"" << ResultToString(summary.result) << "\""
       << ",\"exit_code\":" << summary.exit_code
       << ",\"profile\":\"" << JsonEscape(summary.config.profile) << "\""
       << ",\"api\":\"" << JsonEscape(summary.config.api) << "\""
       << ",\"mode\":\"" << JsonEscape(summary.config.mode) << "\""
       << ",\"backend\":\"" << JsonEscape(summary.config.backend) << "\""
       << ",\"workload_name\":\"" << JsonEscape(summary.config.workload) << "\""
       << ",\"runtime\":\"" << JsonEscape(summary.execution.runtime) << "\""
       << ",\"execution_target\":\"" << JsonEscape(summary.execution.execution_target) << "\""
       << ",\"partitions\":[";
    for (size_t i = 0; i < summary.execution.partitions.size(); ++i) {
        if (i > 0) os << ',';
        os << '\"' << JsonEscape(summary.execution.partitions[i]) << '\"';
    }
    os << "]"
       << ",\"fallback_used\":" << (summary.execution.fallback_used ? "true" : "false")
       << ",\"prepare_time_ms\":" << summary.prepare_time_ms
       << ",\"first_inference_time_ms\":" << summary.first_inference_time_ms
       << ",\"device_time_valid\":" << (summary.device_time.count > 0 ? "true" : "false")
       << ",\"warmup_inferences\":" << summary.actual_warmup_inferences
       << ",\"duration_s\":" << summary.actual_duration_s
       << ",\"work_unit_count\":" << summary.inference_count
       << ",\"inference_count\":" << summary.inference_count
       << ",\"operation_count\":" << summary.operation_count
       << ",\"inferences_per_sec_avg\":" << summary.inferences_per_sec_avg << ',';

    AppendStats(os, "host_inference_time_ms", summary.host_time);
    AppendStats(os, "device_inference_time_ms", summary.device_time);
    AppendStats(os, "window_inferences_per_sec", summary.throughput);

    os << "\"verify_pass\":" << (summary.verify_pass ? "true" : "false")
       << ",\"verify_mode\":\"" << JsonEscape(summary.config.verify_mode) << "\""
       << ",\"verify_fail_count\":" << summary.verify_fail_count
       << ",\"first_fail_inference\":" << summary.first_fail_inference
       << ",\"checksum\":\"" << JsonEscape(summary.checksum) << "\""
       << ",\"golden_checksum\":\"" << JsonEscape(summary.golden_checksum) << "\""
       << ",\"timeout_count\":" << summary.timeout_count
       << ",\"api_error_count\":" << summary.api_error_count
       << ",\"device_lost_count\":" << summary.device_lost_count
       << ",\"allocation_fail_count\":" << summary.allocation_fail_count
       << ",\"backend_error_count\":" << summary.backend_error_count
       << ",\"heartbeat_max_gap_ms\":" << summary.heartbeat_max_gap_ms
       << ",\"heartbeat_last_time_ms\":" << summary.heartbeat_last_time_ms
       << ",\"last_error\":\"" << JsonEscape(summary.last_error) << "\""
       << ",\"last_error_code\":\"" << JsonEscape(summary.last_error_code) << "\"}";
    WriteLine(os.str());
}

uint64_t Logger::LastHeartbeatTimeMs() const { return last_heartbeat_time_ms_; }

} // namespace npu_avs
