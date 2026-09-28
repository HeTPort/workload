#include "npu_avs/runner.h"

#include "npu_avs/backend.h"
#include "npu_avs/heartbeat.h"
#include "npu_avs/logger.h"
#include "npu_avs/metrics.h"
#include "npu_avs/utils.h"
#include "npu_avs/verifier.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <string>

namespace npu_avs {
namespace {

bool StopConditionReached(const WorkloadConfig& cfg, uint64_t count, double elapsed_s) {
    const bool duration_reached = cfg.duration_s > 0.0 && elapsed_s >= cfg.duration_s;
    const bool count_reached = cfg.inferences > 0 && count >= cfg.inferences;
    if (cfg.duration_s > 0.0 && cfg.inferences > 0) return duration_reached || count_reached;
    return cfg.inferences > 0 ? count_reached : duration_reached;
}

TensorBuffer BuildInput(const WorkloadConfig& cfg) {
    TensorBuffer input;
    input.name = "synthetic_input";
    input.data_type = TensorDataType::Int8;
    input.shape = {1U, cfg.input_elements};
    input.scale = 1.0F / 32.0F;
    input.zero_point = 0;
    input.data.resize(cfg.input_elements);
    uint64_t state = cfg.seed;
    for (uint32_t i = 0; i < cfg.input_elements; ++i) {
        state ^= state >> 12U;
        state ^= state << 25U;
        state ^= state >> 27U;
        state *= 2685821657736338717ULL;
        const int8_t value = static_cast<int8_t>(static_cast<int32_t>(state % 255U) - 127);
        input.data[i] = static_cast<uint8_t>(value);
    }
    return input;
}

BackendStatus RunOneInference(INpuBackend& backend, const TensorBuffer& input,
                              uint64_t index, uint32_t timeout_ms,
                              InferenceResult& result, double& host_time_ms,
                              std::string& stage, std::string& error) {
    const double start = NowSeconds();
    if (!backend.SetInput(input, error)) {
        stage = "SET_INPUT_FAILED";
        host_time_ms = (NowSeconds() - start) * 1000.0;
        return BackendStatus::Error;
    }
    BackendStatus status = backend.SubmitInference(index, error);
    if (status != BackendStatus::Ok) {
        stage = "SUBMIT_FAILED";
        host_time_ms = (NowSeconds() - start) * 1000.0;
        return status;
    }
    status = backend.WaitForCompletion(timeout_ms, error);
    if (status != BackendStatus::Ok) {
        stage = status == BackendStatus::Timeout ? "INFERENCE_TIMEOUT" : "WAIT_FAILED";
        host_time_ms = (NowSeconds() - start) * 1000.0;
        return status;
    }
    if (!backend.ReadOutput(result, error)) {
        stage = "READ_OUTPUT_FAILED";
        host_time_ms = (NowSeconds() - start) * 1000.0;
        return BackendStatus::Error;
    }
    host_time_ms = (NowSeconds() - start) * 1000.0;
    stage.clear();
    return BackendStatus::Ok;
}

void CountFailure(ResultCode result, SummaryData& summary) {
    ++summary.backend_error_count;
    if (result == ResultCode::TIMEOUT) ++summary.timeout_count;
    else if (result == ResultCode::DEVICE_LOST) ++summary.device_lost_count;
    else if (result == ResultCode::ALLOCATION_FAIL) ++summary.allocation_fail_count;
    else ++summary.api_error_count;
}

SummaryData BasicSummary(const WorkloadConfig& cfg, const BackendExecutionInfo& execution,
                         ResultCode result, const std::string& error,
                         const std::string& error_code) {
    SummaryData summary;
    summary.result = result;
    summary.exit_code = ResultToExitCode(result);
    summary.config = cfg;
    summary.execution = execution;
    summary.verify_pass = result == ResultCode::PASS;
    summary.last_error = error;
    summary.last_error_code = error_code;
    return summary;
}

} // namespace

ResultCode RunWorkload(const WorkloadConfig& cfg) {
    std::string error;
    Logger logger(cfg);
    if (!logger.Open(error)) return ResultCode::UNKNOWN_ERROR;
    logger.EmitStart(cfg);

    const double total_start = NowSeconds();
    std::unique_ptr<INpuBackend> backend = CreateBackend(cfg);
    if (!backend) {
        const std::string message = "unsupported NPU backend: " + cfg.backend;
        logger.EmitError(NowMs(), 0, "API_ERROR", "UNSUPPORTED_BACKEND", message);
        SummaryData summary = BasicSummary(cfg, {}, ResultCode::API_ERROR, message, "UNSUPPORTED_BACKEND");
        summary.api_error_count = 1;
        summary.backend_error_count = 1;
        logger.EmitSummary(summary);
        return summary.result;
    }

    if (!backend->Init(cfg, error)) {
        logger.EmitError(NowMs(), 0, "API_ERROR", "BACKEND_INIT_FAILED", error);
        SummaryData summary = BasicSummary(cfg, backend->GetExecutionInfo(), ResultCode::API_ERROR,
                                           error, "BACKEND_INIT_FAILED");
        summary.api_error_count = 1;
        summary.backend_error_count = 1;
        logger.EmitSummary(summary);
        backend->Destroy();
        return summary.result;
    }
    if (!backend->CreateResources(error)) {
        logger.EmitError(NowMs(), 0, "ALLOCATION_FAIL", "RESOURCE_CREATE_FAILED", error);
        SummaryData summary = BasicSummary(cfg, backend->GetExecutionInfo(), ResultCode::ALLOCATION_FAIL,
                                           error, "RESOURCE_CREATE_FAILED");
        summary.allocation_fail_count = 1;
        summary.backend_error_count = 1;
        logger.EmitSummary(summary);
        backend->Destroy();
        return summary.result;
    }
    const TensorBuffer input = BuildInput(cfg);
    const double prepare_time_ms = (NowSeconds() - total_start) * 1000.0;
    const BackendExecutionInfo execution = backend->GetExecutionInfo();
    Verifier verifier(cfg);

    uint64_t completed_warmup = 0;
    Heartbeat warmup_heartbeat(cfg.heartbeat_interval_s);
    const double warmup_start = NowSeconds();
    for (; completed_warmup < cfg.warmup_inferences; ++completed_warmup) {
        if (NowSeconds() - total_start > cfg.timeout_s) {
            error = "workload timeout during warmup";
            logger.EmitError(NowMs(), completed_warmup, "TIMEOUT", "TIMEOUT", error);
            SummaryData summary = BasicSummary(cfg, execution, ResultCode::TIMEOUT, error, "TIMEOUT");
            summary.prepare_time_ms = prepare_time_ms;
            summary.actual_warmup_inferences = completed_warmup;
            summary.timeout_count = 1;
            logger.EmitSummary(summary);
            backend->Destroy();
            return summary.result;
        }
        InferenceResult warmup_result;
        double host_ms = 0.0;
        std::string stage;
        const BackendStatus status = RunOneInference(*backend, input, completed_warmup,
            cfg.inference_timeout_ms, warmup_result, host_ms, stage, error);
        if (status != BackendStatus::Ok) {
            const ResultCode result = BackendStatusToResult(status);
            logger.EmitError(NowMs(), completed_warmup, ResultToString(result), stage, error);
            SummaryData summary = BasicSummary(cfg, execution, result, error, stage);
            summary.prepare_time_ms = prepare_time_ms;
            summary.actual_warmup_inferences = completed_warmup;
            CountFailure(result, summary);
            logger.EmitSummary(summary);
            backend->Destroy();
            return summary.result;
        }
        double ignored_rate = 0.0;
        warmup_heartbeat.MaybeEmit(logger, NowMs(), NowSeconds() - warmup_start, "warmup",
            completed_warmup + 1U, warmup_result.operation_count, host_ms,
            warmup_result.device_time_ms, 0, 0, ignored_rate);
    }

    if (cfg.generate_golden) {
        InferenceResult result;
        double host_ms = 0.0;
        std::string stage;
        const BackendStatus status = RunOneInference(*backend, input, 0, cfg.inference_timeout_ms,
                                                      result, host_ms, stage, error);
        if (status != BackendStatus::Ok) {
            const ResultCode code = BackendStatusToResult(status);
            logger.EmitError(NowMs(), 0, ResultToString(code), stage, error);
            SummaryData summary = BasicSummary(cfg, execution, code, error, stage);
            summary.prepare_time_ms = prepare_time_ms;
            summary.actual_warmup_inferences = completed_warmup;
            CountFailure(code, summary);
            logger.EmitSummary(summary);
            backend->Destroy();
            return code;
        }
        const std::string checksum = verifier.ComputeChecksum(result.output);
        logger.EmitGolden(cfg, checksum);
        SummaryData summary = BasicSummary(cfg, execution, ResultCode::PASS, "", "");
        summary.prepare_time_ms = prepare_time_ms;
        summary.first_inference_time_ms = host_ms;
        summary.actual_warmup_inferences = completed_warmup;
        summary.inference_count = 1;
        summary.operation_count = result.operation_count;
        summary.host_time = MetricsCollector::ComputeStats({host_ms});
        if (result.device_time_valid) summary.device_time = MetricsCollector::ComputeStats({result.device_time_ms});
        summary.checksum = checksum;
        summary.golden_checksum = checksum;
        logger.EmitSummary(summary);
        backend->Destroy();
        return summary.result;
    }

    MetricsCollector metrics;
    Heartbeat heartbeat(cfg.heartbeat_interval_s);
    uint64_t inference_count = 0;
    uint64_t operation_count = 0;
    uint64_t verify_fail_count = 0;
    uint64_t backend_error_count = 0;
    int64_t first_fail_inference = -1;
    double first_inference_time_ms = 0.0;
    double last_host_ms = 0.0;
    double last_device_ms = 0.0;
    std::string last_checksum;
    std::string last_golden;
    std::string last_error;
    std::string last_error_code;
    ResultCode final_result = ResultCode::PASS;
    SummaryData counters;
    const double run_start = NowSeconds();

    while (true) {
        const double run_elapsed = NowSeconds() - run_start;
        if (NowSeconds() - total_start > cfg.timeout_s) {
            final_result = ResultCode::TIMEOUT;
            ++counters.timeout_count;
            ++backend_error_count;
            last_error = "workload timeout";
            last_error_code = "TIMEOUT";
            logger.EmitError(NowMs(), inference_count, "TIMEOUT", last_error_code, last_error);
            break;
        }
        if (StopConditionReached(cfg, inference_count, run_elapsed)) break;

        InferenceResult inference;
        std::string stage;
        const BackendStatus status = RunOneInference(*backend, input, inference_count,
            cfg.inference_timeout_ms, inference, last_host_ms, stage, error);
        if (status != BackendStatus::Ok) {
            final_result = BackendStatusToResult(status);
            CountFailure(final_result, counters);
            ++backend_error_count;
            last_error = error.empty() ? "NPU backend inference failed" : error;
            last_error_code = stage;
            logger.EmitError(NowMs(), inference_count, ResultToString(final_result), stage, last_error);
            break;
        }

        ++inference_count;
        if (inference_count == 1) first_inference_time_ms = last_host_ms;
        if (inference.operation_count > std::numeric_limits<uint64_t>::max() - operation_count) {
            operation_count = std::numeric_limits<uint64_t>::max();
        } else {
            operation_count += inference.operation_count;
        }
        metrics.AddHostInferenceTime(last_host_ms);
        if (inference.device_time_valid) {
            last_device_ms = inference.device_time_ms;
            metrics.AddDeviceInferenceTime(last_device_ms);
        } else {
            last_device_ms = 0.0;
        }
        last_checksum = verifier.ComputeChecksum(inference.output);
        logger.EmitInference(inference_count, inference, last_host_ms, last_checksum);

        if (verifier.Enabled() && cfg.verify_interval > 0 &&
            inference_count % cfg.verify_interval == 0) {
            const VerifyResult verification = verifier.Verify(inference.output, inference_count);
            last_golden = verification.golden_checksum;
            logger.EmitVerify({verification.inference, verification.verify_mode,
                verification.checksum, verification.golden_checksum, verification.pass,
                verification.mismatch_count, verification.message});
            if (!verification.pass) {
                ++verify_fail_count;
                if (first_fail_inference < 0) first_fail_inference = static_cast<int64_t>(inference_count);
                final_result = ResultCode::CHECKSUM_FAIL;
                last_error = verification.message;
                last_error_code = "VERIFY_FAILED";
                if (cfg.fail_fast) break;
            }
        }

        double throughput = 0.0;
        if (heartbeat.MaybeEmit(logger, NowMs(), NowSeconds() - run_start, "running",
                inference_count, operation_count, last_host_ms, last_device_ms,
                verify_fail_count, backend_error_count, throughput)) {
            metrics.AddThroughputSample(throughput);
        }
    }

    const double actual_duration_s = NowSeconds() - run_start;
    backend->Destroy();

    SummaryData summary;
    summary.result = final_result;
    summary.exit_code = ResultToExitCode(final_result);
    summary.config = cfg;
    summary.execution = execution;
    summary.inference_count = inference_count;
    summary.operation_count = operation_count;
    summary.prepare_time_ms = prepare_time_ms;
    summary.first_inference_time_ms = first_inference_time_ms;
    summary.actual_duration_s = actual_duration_s;
    summary.actual_warmup_inferences = completed_warmup;
    summary.inferences_per_sec_avg = actual_duration_s > 0.0 ? inference_count / actual_duration_s : 0.0;
    summary.host_time = metrics.HostInferenceTimeStats();
    summary.device_time = metrics.DeviceInferenceTimeStats();
    summary.throughput = metrics.ThroughputStats();
    if (summary.throughput.count == 0 && actual_duration_s > 0.0) {
        summary.throughput = MetricsCollector::ComputeStats({summary.inferences_per_sec_avg});
    }
    summary.verify_pass = verify_fail_count == 0 && final_result == ResultCode::PASS;
    summary.verify_fail_count = verify_fail_count;
    summary.first_fail_inference = first_fail_inference;
    summary.checksum = last_checksum;
    summary.golden_checksum = last_golden.empty() ? cfg.golden_checksum : last_golden;
    summary.timeout_count = counters.timeout_count;
    summary.api_error_count = counters.api_error_count;
    summary.device_lost_count = counters.device_lost_count;
    summary.allocation_fail_count = counters.allocation_fail_count;
    summary.backend_error_count = backend_error_count;
    summary.heartbeat_max_gap_ms = heartbeat.MaxGapMs();
    summary.heartbeat_last_time_ms = logger.LastHeartbeatTimeMs();
    summary.last_error = last_error;
    summary.last_error_code = last_error_code;
    logger.EmitSummary(summary);
    return final_result;
}

} // namespace npu_avs
