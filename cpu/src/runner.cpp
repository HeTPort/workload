#include "cpu_avs/runner.h"

#include "cpu_avs/backend.h"
#include "cpu_avs/crc32.h"
#include "cpu_avs/heartbeat.h"
#include "cpu_avs/logger.h"
#include "cpu_avs/metrics.h"
#include "cpu_avs/utils.h"
#include "cpu_avs/verifier.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace cpu_avs {
namespace {

bool StopConditionReached(const WorkloadConfig& cfg, uint64_t batches, double elapsed_s) {
    const bool duration_reached = cfg.duration_s > 0.0 && elapsed_s >= cfg.duration_s;
    const bool batches_reached = cfg.batches > 0 && batches >= cfg.batches;
    if (cfg.duration_s > 0.0 && cfg.batches > 0) return duration_reached || batches_reached;
    return cfg.batches > 0 ? batches_reached : duration_reached;
}

bool IsActivePeriod(const WorkloadConfig& cfg, double elapsed_s) {
    if (cfg.duty_cycle <= 0.0) return false;
    if (cfg.duty_cycle >= 1.0 || cfg.burst_period_s <= 0.0) return true;
    const double phase = std::fmod(elapsed_s, cfg.burst_period_s);
    const double active_s = std::min(cfg.burst_active_s, cfg.burst_period_s);
    return phase < active_s;
}

SummaryData BasicSummary(const WorkloadConfig& cfg, ResultCode result,
                         const std::string& error, const std::string& error_code) {
    SummaryData summary;
    summary.result = result;
    summary.exit_code = ResultToExitCode(result);
    summary.config = cfg;
    summary.verify_pass = result != ResultCode::CHECKSUM_FAIL;
    summary.last_error = error;
    summary.last_error_code = error_code;
    return summary;
}

void AppendWarning(std::string& warnings, const std::string& warning) {
    if (!warnings.empty()) warnings += ',';
    warnings += warning;
}

} // namespace

ResultCode RunWorkload(const WorkloadConfig& cfg) {
    std::string error;
    Logger logger(cfg);
    if (!logger.Open(error)) return ResultCode::UNKNOWN_ERROR;
    logger.EmitStart(cfg);

    const double total_start = NowSeconds();
    std::unique_ptr<ICpuBackend> backend = CreateBackend(cfg);
    if (!backend) {
        const std::string message = "unsupported CPU backend: " + cfg.backend;
        logger.EmitError(NowMs(), 0, "API_ERROR", "UNSUPPORTED_BACKEND", message);
        SummaryData summary = BasicSummary(cfg, ResultCode::API_ERROR, message, "UNSUPPORTED_BACKEND");
        summary.api_error_count = 1;
        logger.EmitSummary(summary);
        return summary.result;
    }

    if (!backend->Init(cfg, error)) {
        logger.EmitError(NowMs(), 0, "API_ERROR", "BACKEND_INIT_FAILED", error);
        SummaryData summary = BasicSummary(cfg, ResultCode::API_ERROR, error, "BACKEND_INIT_FAILED");
        summary.api_error_count = 1;
        logger.EmitSummary(summary);
        backend->Destroy();
        return summary.result;
    }
    if (!backend->CreateResources(error)) {
        logger.EmitError(NowMs(), 0, "ALLOCATION_FAIL", "RESOURCE_CREATE_FAILED", error);
        SummaryData summary = BasicSummary(cfg, ResultCode::ALLOCATION_FAIL, error, "RESOURCE_CREATE_FAILED");
        summary.allocation_fail_count = 1;
        logger.EmitSummary(summary);
        backend->Destroy();
        return summary.result;
    }

    Verifier verifier(cfg);
    uint64_t warmup_batches = 0;
    uint64_t warmup_operations = 0;
    double last_warmup_batch_ms = 0.0;
    Heartbeat warmup_heartbeat(cfg.heartbeat_interval_s);
    const double warmup_start = NowSeconds();

    while (NowSeconds() - warmup_start < cfg.warmup_s) {
        if (NowSeconds() - total_start > cfg.timeout_s) {
            error = "workload timeout during warmup";
            logger.EmitError(NowMs(), warmup_batches, "TIMEOUT", "TIMEOUT", error);
            SummaryData summary = BasicSummary(cfg, ResultCode::TIMEOUT, error, "TIMEOUT");
            summary.timeout_count = 1;
            summary.actual_warmup_s = NowSeconds() - warmup_start;
            logger.EmitSummary(summary);
            backend->Destroy();
            return summary.result;
        }

        const double start = NowSeconds();
        CpuBatchResult batch;
        const BackendStatus status = backend->RunBatch(warmup_batches, batch, error);
        last_warmup_batch_ms = (NowSeconds() - start) * 1000.0;
        if (status != BackendStatus::Ok) {
            const ResultCode result = BackendStatusToResult(status);
            logger.EmitError(NowMs(), warmup_batches, ResultToString(result), "WARMUP_BACKEND_FAILED", error);
            SummaryData summary = BasicSummary(cfg, result, error, "WARMUP_BACKEND_FAILED");
            if (result == ResultCode::ALLOCATION_FAIL) summary.allocation_fail_count = 1;
            else summary.api_error_count = 1;
            summary.actual_warmup_s = NowSeconds() - warmup_start;
            logger.EmitSummary(summary);
            backend->Destroy();
            return summary.result;
        }
        ++warmup_batches;
        warmup_operations += batch.operation_count;
        double ignored_throughput = 0.0;
        warmup_heartbeat.MaybeEmit(logger, NowMs(), NowSeconds() - warmup_start, "warmup",
            warmup_batches, warmup_operations, last_warmup_batch_ms, 0, 0, ignored_throughput);
    }
    const double actual_warmup_s = NowSeconds() - warmup_start;

    if (cfg.generate_golden) {
        CpuBatchResult batch;
        const double start = NowSeconds();
        const BackendStatus status = backend->RunBatch(0, batch, error);
        const double batch_ms = (NowSeconds() - start) * 1000.0;
        if (status != BackendStatus::Ok) {
            const ResultCode result = BackendStatusToResult(status);
            logger.EmitError(NowMs(), 0, ResultToString(result), "GOLDEN_BACKEND_FAILED", error);
            SummaryData summary = BasicSummary(cfg, result, error, "GOLDEN_BACKEND_FAILED");
            logger.EmitSummary(summary);
            backend->Destroy();
            return result;
        }
        const std::string checksum = verifier.ComputeChecksum(batch);
        logger.EmitGolden(cfg, checksum);
        SummaryData summary = BasicSummary(cfg, ResultCode::PASS, "", "");
        summary.actual_warmup_s = actual_warmup_s;
        summary.batch_count = 1;
        summary.operation_count = batch.operation_count;
        summary.batch_time = MetricsCollector::ComputeStats({batch_ms});
        summary.checksum = checksum;
        CpuBatchResult expected_batch;
        expected_batch.checksum = backend->ExpectedChecksum();
        summary.golden_checksum = verifier.ComputeChecksum(expected_batch);
        summary.operations_per_sec_avg = batch_ms > 0.0 ? batch.operation_count * 1000.0 / batch_ms : 0.0;
        summary.batches_per_sec_avg = batch_ms > 0.0 ? 1000.0 / batch_ms : 0.0;
        logger.EmitSummary(summary);
        backend->Destroy();
        return summary.result;
    }

    MetricsCollector metrics;
    Heartbeat heartbeat(cfg.heartbeat_interval_s);
    uint64_t batch_count = 0;
    uint64_t operation_count = 0;
    uint64_t verify_fail_count = 0;
    uint64_t worker_error_count = 0;
    uint64_t timeout_count = 0;
    uint64_t api_error_count = 0;
    uint64_t allocation_fail_count = 0;
    int64_t first_fail_batch = -1;
    double last_batch_ms = 0.0;
    std::string last_checksum;
    std::string last_error;
    std::string last_error_code;
    ResultCode final_result = ResultCode::PASS;
    const double run_start = NowSeconds();

    while (true) {
        const double now = NowSeconds();
        const double run_elapsed = now - run_start;
        const double total_elapsed = now - total_start;

        if (total_elapsed > cfg.timeout_s) {
            final_result = ResultCode::TIMEOUT;
            ++timeout_count;
            last_error = "workload timeout";
            last_error_code = "TIMEOUT";
            logger.EmitError(NowMs(), batch_count, "TIMEOUT", last_error_code, last_error);
            break;
        }
        if (StopConditionReached(cfg, batch_count, run_elapsed)) break;

        if (!IsActivePeriod(cfg, run_elapsed)) {
            SleepMs(1U);
            double throughput = 0.0;
            if (heartbeat.MaybeEmit(logger, NowMs(), NowSeconds() - run_start, "running",
                    batch_count, operation_count, last_batch_ms, verify_fail_count,
                    worker_error_count, throughput)) {
                metrics.AddThroughputSample(throughput);
            }
            continue;
        }

        CpuBatchResult batch;
        const double batch_start = NowSeconds();
        const BackendStatus status = backend->RunBatch(batch_count, batch, error);
        last_batch_ms = (NowSeconds() - batch_start) * 1000.0;

        if (status != BackendStatus::Ok) {
            final_result = BackendStatusToResult(status);
            ++worker_error_count;
            if (final_result == ResultCode::ALLOCATION_FAIL) ++allocation_fail_count;
            else ++api_error_count;
            last_error = error.empty() ? "CPU backend execution failed" : error;
            last_error_code = "BACKEND_RUN_FAILED";
            logger.EmitError(NowMs(), batch_count, ResultToString(final_result),
                             last_error_code, last_error);
            break;
        }
        if (cfg.batch_timeout_ms > 0 && last_batch_ms > cfg.batch_timeout_ms) {
            final_result = ResultCode::TIMEOUT;
            ++timeout_count;
            last_error = "CPU batch exceeded batch timeout";
            last_error_code = "BATCH_TIMEOUT";
            logger.EmitError(NowMs(), batch_count, "TIMEOUT", last_error_code, last_error);
            break;
        }

        ++batch_count;
        operation_count = batch.operation_count > UINT64_MAX - operation_count
            ? UINT64_MAX
            : operation_count + batch.operation_count;
        metrics.AddBatchTime(last_batch_ms);
        last_checksum = verifier.ComputeChecksum(batch);
        logger.EmitBatch(batch_count, batch.operation_count, last_batch_ms, last_checksum);

        if (verifier.Enabled()) {
            const VerifyResult verification = verifier.Verify(batch, backend->ExpectedChecksum(), batch_count);
            const bool emit_verify = !verification.pass ||
                (cfg.checksum_interval > 0 && batch_count % cfg.checksum_interval == 0);
            if (emit_verify) {
                VerifyData data;
                data.batch = verification.batch;
                data.verify_mode = verification.verify_mode;
                data.checksum = verification.checksum;
                data.golden_checksum = verification.golden_checksum;
                data.pass = verification.pass;
                data.mismatch_count = verification.mismatch_count;
                data.message = verification.message;
                logger.EmitVerify(data);
            }
            if (!verification.pass) {
                ++verify_fail_count;
                if (first_fail_batch < 0) first_fail_batch = static_cast<int64_t>(batch_count);
                final_result = ResultCode::CHECKSUM_FAIL;
                last_error = verification.message;
                last_error_code = "VERIFY_FAILED";
                if (cfg.fail_fast) break;
            }
        }

        double throughput = 0.0;
        if (heartbeat.MaybeEmit(logger, NowMs(), NowSeconds() - run_start, "running",
                batch_count, operation_count, last_batch_ms, verify_fail_count,
                worker_error_count, throughput)) {
            metrics.AddThroughputSample(throughput);
        }
    }

    const double actual_duration_s = NowSeconds() - run_start;
    backend->Destroy();

    SummaryData summary;
    summary.result = final_result;
    summary.config = cfg;
    summary.batch_count = batch_count;
    summary.operation_count = operation_count;
    summary.actual_duration_s = actual_duration_s;
    summary.actual_warmup_s = actual_warmup_s;
    summary.operations_per_sec_avg = actual_duration_s > 0.0
        ? static_cast<double>(operation_count) / actual_duration_s : 0.0;
    summary.batches_per_sec_avg = actual_duration_s > 0.0
        ? static_cast<double>(batch_count) / actual_duration_s : 0.0;
    summary.batch_time = metrics.BatchTimeStats();
    summary.throughput = metrics.ThroughputStats();
    if (summary.throughput.count == 0 && actual_duration_s > 0.0) {
        summary.throughput = MetricsCollector::ComputeStats({summary.operations_per_sec_avg});
    }
    summary.verify_pass = verify_fail_count == 0;
    summary.verify_fail_count = verify_fail_count;
    summary.first_fail_batch = first_fail_batch;
    summary.checksum = last_checksum;
    CpuBatchResult expected_batch;
    expected_batch.checksum = backend->ExpectedChecksum();
    summary.golden_checksum = verifier.ComputeChecksum(expected_batch);
    summary.timeout_count = timeout_count;
    summary.api_error_count = api_error_count;
    summary.allocation_fail_count = allocation_fail_count;
    summary.worker_error_count = worker_error_count;
    summary.heartbeat_max_gap_ms = heartbeat.MaxGapMs();
    summary.heartbeat_last_time_ms = logger.LastHeartbeatTimeMs();
    summary.last_error = last_error;
    summary.last_error_code = last_error_code;

    if (cfg.min_operations_per_sec > 0.0 &&
        summary.operations_per_sec_avg < cfg.min_operations_per_sec) {
        summary.performance_stable = false;
        AppendWarning(summary.performance_warning, "MIN_OPERATIONS_PER_SEC");
    }
    if (cfg.max_throughput_cv_pct > 0.0 &&
        summary.throughput.cv_pct > cfg.max_throughput_cv_pct) {
        summary.performance_stable = false;
        AppendWarning(summary.performance_warning, "THROUGHPUT_CV_EXCEEDED");
    }
    if (cfg.max_batch_p99_ms > 0.0 && summary.batch_time.p99 > cfg.max_batch_p99_ms) {
        summary.performance_stable = false;
        AppendWarning(summary.performance_warning, "BATCH_P99_EXCEEDED");
    }
    if (cfg.max_heartbeat_gap_s > 0.0 &&
        summary.heartbeat_max_gap_ms > cfg.max_heartbeat_gap_s * 1000.0) {
        summary.performance_stable = false;
        AppendWarning(summary.performance_warning, "HEARTBEAT_GAP_EXCEEDED");
    }

    if (!summary.performance_stable && cfg.fail_on_instability &&
        summary.result == ResultCode::PASS) {
        summary.result = ResultCode::PERFORMANCE_FAIL;
        summary.last_error = "configured performance stability threshold exceeded";
        summary.last_error_code = summary.performance_warning;
    }
    summary.exit_code = ResultToExitCode(summary.result);
    logger.EmitSummary(summary);
    return summary.result;
}

} // namespace cpu_avs
