#include "gpu_avs/runner.h"

#include "gpu_avs/backend.h"
#include "gpu_avs/heartbeat.h"
#include "gpu_avs/logger.h"
#include "gpu_avs/metrics.h"
#include "gpu_avs/utils.h"
#include "gpu_avs/verifier.h"

#include <memory>
#include <string>

namespace gpu_avs {

static bool StopConditionReached(
    const WorkloadConfig& cfg,
    uint64_t frame_count,
    double running_elapsed_s
) {
    bool duration_reached =
        cfg.duration_s > 0.0 &&
        running_elapsed_s >= cfg.duration_s;

    bool frames_reached =
        cfg.frames > 0 &&
        frame_count >= cfg.frames;

    if (cfg.duration_s > 0.0 && cfg.frames > 0) {
        return duration_reached || frames_reached;
    }

    if (cfg.frames > 0) {
        return frames_reached;
    }

    return duration_reached;
}

static void FillErrorCounters(
    ResultCode r,
    uint64_t& timeout_count,
    uint64_t& api_error_count,
    uint64_t& device_lost_count,
    uint64_t& allocation_fail_count
) {
    if (r == ResultCode::TIMEOUT) {
        timeout_count++;
    } else if (r == ResultCode::API_ERROR) {
        api_error_count++;
    } else if (r == ResultCode::DEVICE_LOST) {
        device_lost_count++;
    } else if (r == ResultCode::ALLOCATION_FAIL) {
        allocation_fail_count++;
    }
}

static ResultCode BackendFailureToResult(
    IGpuBackend* backend,
    ResultCode fallback
) {
    if (!backend) {
        return fallback;
    }

    SubmitStatus st = backend->LastStatus();
    ResultCode r = SubmitStatusToResult(st);

    if (r == ResultCode::PASS) {
        return fallback;
    }

    return r;
}

static std::string ResultToErrorCodeForBackendFailure(ResultCode r) {
    if (r == ResultCode::DEVICE_LOST) {
        return "DEVICE_LOST";
    }

    if (r == ResultCode::ALLOCATION_FAIL) {
        return "ALLOCATION_FAIL";
    }

    if (r == ResultCode::API_ERROR) {
        return "API_ERROR";
    }

    if (r == ResultCode::TIMEOUT) {
        return "GPU_TIMEOUT";
    }

    return "BACKEND_ERROR";
}

static SummaryData MakeBasicSummary(
    const WorkloadConfig& cfg,
    ResultCode result,
    const std::string& last_error,
    const std::string& last_error_code
) {
    SummaryData summary;
    summary.result = result;
    summary.exit_code = ResultToExitCode(result);
    summary.config = cfg;
    summary.verify_mode = cfg.verify_mode;
    summary.verify_pass = result == ResultCode::PASS;
    summary.first_fail_frame = -1;
    summary.golden_checksum = cfg.golden_checksum;
    summary.last_error = last_error;
    summary.last_error_code = last_error_code;
    return summary;
}

ResultCode RunWorkload(const WorkloadConfig& cfg) {
    std::string error;

    Logger logger(cfg);
    if (!logger.Open(error)) {
        return ResultCode::UNKNOWN_ERROR;
    }

    logger.EmitStart(cfg);

    auto backend = CreateBackend(cfg);
    if (!backend) {
        std::string msg = "unsupported backend api/mode: api=" + cfg.api + ", mode=" + cfg.mode;

        logger.EmitError(
            NowMs(),
            0,
            "API_ERROR",
            "UNSUPPORTED_BACKEND",
            msg
        );

        SummaryData summary = MakeBasicSummary(
            cfg,
            ResultCode::API_ERROR,
            msg,
            "UNSUPPORTED_BACKEND"
        );

        summary.api_error_count = 1;
        logger.EmitSummary(summary);
        return summary.result;
    }

    if (!backend->Init(cfg, error)) {
        ResultCode r = BackendFailureToResult(
            backend.get(),
            ResultCode::API_ERROR
        );

        logger.EmitError(
            NowMs(),
            0,
            ResultToString(r),
            ResultToErrorCodeForBackendFailure(r),
            error
        );

        SummaryData summary = MakeBasicSummary(
            cfg,
            r,
            error,
            ResultToErrorCodeForBackendFailure(r)
        );

        if (r == ResultCode::API_ERROR) summary.api_error_count = 1;
        if (r == ResultCode::DEVICE_LOST) summary.device_lost_count = 1;
        if (r == ResultCode::ALLOCATION_FAIL) summary.allocation_fail_count = 1;
        if (r == ResultCode::TIMEOUT) summary.timeout_count = 1;

        logger.EmitSummary(summary);
        backend->Destroy();
        return summary.result;
    }

    if (!backend->CreateResources(error)) {
        ResultCode r = BackendFailureToResult(
            backend.get(),
            ResultCode::ALLOCATION_FAIL
        );

        logger.EmitError(
            NowMs(),
            0,
            ResultToString(r),
            ResultToErrorCodeForBackendFailure(r),
            error
        );

        SummaryData summary = MakeBasicSummary(
            cfg,
            r,
            error,
            ResultToErrorCodeForBackendFailure(r)
        );

        if (r == ResultCode::API_ERROR) summary.api_error_count = 1;
        if (r == ResultCode::DEVICE_LOST) summary.device_lost_count = 1;
        if (r == ResultCode::ALLOCATION_FAIL) summary.allocation_fail_count = 1;
        if (r == ResultCode::TIMEOUT) summary.timeout_count = 1;

        logger.EmitSummary(summary);
        backend->Destroy();
        return summary.result;
    }

    Heartbeat heartbeat(cfg.heartbeat_interval_s);
    MetricsCollector metrics;
    Verifier verifier(cfg);

    const double total_start = NowSeconds();

    uint64_t warmup_frames = 0;
    double last_frame_ms = 0.0;
    double last_gpu_ms = 0.0;

    const double warmup_start = NowSeconds();

    while (NowSeconds() - warmup_start < cfg.warmup_s) {
        const double elapsed_total = NowSeconds() - total_start;

        if (elapsed_total > cfg.timeout_s) {
            SummaryData summary = MakeBasicSummary(
                cfg,
                ResultCode::TIMEOUT,
                "timeout during warmup",
                "TIMEOUT"
            );

            summary.timeout_count = 1;
            summary.actual_warmup_s = NowSeconds() - warmup_start;
            summary.heartbeat_last_time_ms = logger.LastHeartbeatTimeMs();

            logger.EmitSummary(summary);
            backend->Destroy();
            return summary.result;
        }

        const double frame_start = NowSeconds();

        SubmitStatus st = backend->SubmitWorkload(warmup_frames);
        if (st != SubmitStatus::Ok) {
            ResultCode r = SubmitStatusToResult(st);
            std::string msg = "submit failed during warmup";

            logger.EmitError(
                NowMs(),
                warmup_frames,
                ResultToString(r),
                "SUBMIT_FAILED",
                msg
            );

            SummaryData summary = MakeBasicSummary(
                cfg,
                r,
                msg,
                "SUBMIT_FAILED"
            );

            FillErrorCounters(
                r,
                summary.timeout_count,
                summary.api_error_count,
                summary.device_lost_count,
                summary.allocation_fail_count
            );

            logger.EmitSummary(summary);
            backend->Destroy();
            return r;
        }

        if (!backend->WaitIdleOrFrameDone(
                static_cast<uint64_t>(cfg.gpu_timeout_ms) * 1000000ULL,
                error)) {
            ResultCode r = BackendFailureToResult(
                backend.get(),
                ResultCode::TIMEOUT
            );

            SummaryData summary = MakeBasicSummary(
                cfg,
                r,
                error.empty() ? "gpu wait failed during warmup" : error,
                ResultToErrorCodeForBackendFailure(r)
            );

            FillErrorCounters(
                r,
                summary.timeout_count,
                summary.api_error_count,
                summary.device_lost_count,
                summary.allocation_fail_count
            );

            summary.actual_warmup_s = NowSeconds() - warmup_start;
            summary.heartbeat_last_time_ms = logger.LastHeartbeatTimeMs();

            logger.EmitSummary(summary);
            backend->Destroy();
            return summary.result;
        }

        last_frame_ms = (NowSeconds() - frame_start) * 1000.0;

        if (backend->SupportsGpuTimestamp()) {
            backend->GetLastGpuTimeMs(last_gpu_ms);
        }

        warmup_frames++;

        heartbeat.MaybeEmit(
            logger,
            elapsed_total,
            "warmup",
            warmup_frames,
            warmup_frames,
            last_frame_ms,
            last_gpu_ms
        );
    }

    const double actual_warmup_s = NowSeconds() - warmup_start;

    if (cfg.generate_golden) {
        ResultCode result = ResultCode::PASS;
        std::string last_error;
        std::string last_error_code;

        SubmitStatus st = backend->SubmitWorkload(0);
        if (st != SubmitStatus::Ok) {
            result = SubmitStatusToResult(st);
            last_error = "submit failed during golden generation";
            last_error_code = "SUBMIT_FAILED";

            logger.EmitError(
                NowMs(),
                0,
                ResultToString(result),
                last_error_code,
                last_error
            );
        }

        if (result == ResultCode::PASS) {
            if (!backend->WaitIdleOrFrameDone(
                    static_cast<uint64_t>(cfg.gpu_timeout_ms) * 1000000ULL,
                    error)) {
                result = BackendFailureToResult(
                    backend.get(),
                    ResultCode::TIMEOUT
                );

                last_error = error.empty()
                    ? "gpu wait failed during golden generation"
                    : error;

                last_error_code = ResultToErrorCodeForBackendFailure(result);

                logger.EmitError(
                    NowMs(),
                    0,
                    ResultToString(result),
                    last_error_code,
                    last_error
                );
            }
        }

        ReadbackBuffer rb;
        std::string checksum;

        if (result == ResultCode::PASS) {
            if (!backend->Readback(rb, error)) {
                result = BackendFailureToResult(
                    backend.get(),
                    ResultCode::API_ERROR
                );

                last_error = error.empty()
                    ? "readback failed during golden generation"
                    : error;

                last_error_code = ResultToErrorCodeForBackendFailure(result);

                logger.EmitError(
                    NowMs(),
                    0,
                    ResultToString(result),
                    last_error_code,
                    last_error
                );
            } else {
                checksum = verifier.ComputeChecksum(rb);

                if (!cfg.golden_file.empty()) {
                    std::string write_error;
                    if (!verifier.WriteGoldenFile(rb, cfg.golden_file, write_error)) {
                        result = ResultCode::API_ERROR;
                        last_error = write_error;
                        last_error_code = "WRITE_GOLDEN_FAILED";

                        logger.EmitError(
                            NowMs(),
                            0,
                            "API_ERROR",
                            last_error_code,
                            last_error
                        );
                    }
                }
            }
        }

        if (result == ResultCode::PASS) {
            logger.EmitGolden(cfg, checksum, cfg.golden_file);
        }

        SummaryData summary = MakeBasicSummary(
            cfg,
            result,
            last_error,
            last_error_code
        );

        summary.actual_warmup_s = actual_warmup_s;
        summary.actual_duration_s = 0.0;
        summary.frame_count = result == ResultCode::PASS ? 1 : 0;
        summary.verify_pass = result == ResultCode::PASS;
        summary.verify_mode = cfg.verify_mode;
        summary.checksum = checksum;
        summary.golden_checksum = cfg.golden_checksum;
        summary.heartbeat_last_time_ms = logger.LastHeartbeatTimeMs();

        FillErrorCounters(
            result,
            summary.timeout_count,
            summary.api_error_count,
            summary.device_lost_count,
            summary.allocation_fail_count
        );

        logger.EmitSummary(summary);
        backend->Destroy();
        return result;
    }

    uint64_t frame_index = 0;
    uint64_t gpu_job_count = 0;

    uint64_t timeout_count = 0;
    uint64_t api_error_count = 0;
    uint64_t device_lost_count = 0;
    uint64_t allocation_fail_count = 0;

    uint64_t verify_fail_count = 0;
    int64_t first_fail_frame = -1;
    uint64_t pixel_diff_count = 0;
    uint64_t compute_mismatch_count = 0;

    std::string last_checksum;
    std::string last_error;
    std::string last_error_code;

    ResultCode final_result = ResultCode::PASS;

    const double run_start = NowSeconds();

    while (true) {
        const double now = NowSeconds();
        const double total_elapsed = now - total_start;
        const double run_elapsed = now - run_start;

        if (total_elapsed > cfg.timeout_s) {
            final_result = ResultCode::TIMEOUT;
            timeout_count++;
            last_error = "workload timeout";
            last_error_code = "TIMEOUT";

            logger.EmitError(
                NowMs(),
                frame_index,
                "TIMEOUT",
                "TIMEOUT",
                last_error
            );

            break;
        }

        if (StopConditionReached(cfg, frame_index, run_elapsed)) {
            break;
        }

        const double frame_start = NowSeconds();

        SubmitStatus st = backend->SubmitWorkload(frame_index);
        if (st != SubmitStatus::Ok) {
            final_result = SubmitStatusToResult(st);
            last_error = "submit workload failed";
            last_error_code = "SUBMIT_FAILED";

            FillErrorCounters(
                final_result,
                timeout_count,
                api_error_count,
                device_lost_count,
                allocation_fail_count
            );

            logger.EmitError(
                NowMs(),
                frame_index,
                ResultToString(final_result),
                last_error_code,
                last_error
            );

            break;
        }

        if (!backend->WaitIdleOrFrameDone(
                static_cast<uint64_t>(cfg.gpu_timeout_ms) * 1000000ULL,
                error)) {
            final_result = BackendFailureToResult(
                backend.get(),
                ResultCode::TIMEOUT
            );

            FillErrorCounters(
                final_result,
                timeout_count,
                api_error_count,
                device_lost_count,
                allocation_fail_count
            );

            last_error = error.empty() ? "gpu wait failed" : error;
            last_error_code = ResultToErrorCodeForBackendFailure(final_result);

            logger.EmitError(
                NowMs(),
                frame_index,
                ResultToString(final_result),
                last_error_code,
                last_error
            );

            break;
        }

        gpu_job_count++;

        last_frame_ms = (NowSeconds() - frame_start) * 1000.0;
        metrics.AddFrameTime(last_frame_ms);

        if (cfg.gpu_timestamp && backend->SupportsGpuTimestamp()) {
            if (backend->GetLastGpuTimeMs(last_gpu_ms)) {
                metrics.AddGpuTime(last_gpu_ms);
            }
        }

        const uint64_t completed_frame = frame_index + 1;

        if (verifier.Enabled() &&
            cfg.checksum_interval > 0 &&
            completed_frame % cfg.checksum_interval == 0) {
            ReadbackBuffer rb;

            if (!backend->Readback(rb, error)) {
                final_result = BackendFailureToResult(
                    backend.get(),
                    ResultCode::API_ERROR
                );

                FillErrorCounters(
                    final_result,
                    timeout_count,
                    api_error_count,
                    device_lost_count,
                    allocation_fail_count
                );

                last_error = error.empty() ? "readback failed" : error;
                last_error_code = ResultToErrorCodeForBackendFailure(final_result);

                logger.EmitError(
                    NowMs(),
                    completed_frame,
                    ResultToString(final_result),
                    last_error_code,
                    last_error
                );

                break;
            }

            VerifyResult vr = verifier.Verify(rb, completed_frame);
            last_checksum = vr.checksum;

            logger.EmitVerify(
                completed_frame,
                vr.verify_mode,
                vr.checksum,
                vr.golden_checksum,
                vr.pass,
                vr.mismatch_count,
                vr.pixel_diff_count,
                vr.compute_mismatch_count,
                vr.message
            );

            if (!vr.pass) {
                verify_fail_count++;

                if (first_fail_frame < 0) {
                    first_fail_frame = static_cast<int64_t>(completed_frame);
                }

                pixel_diff_count += vr.pixel_diff_count;
                compute_mismatch_count += vr.compute_mismatch_count;

                final_result = ResultCode::CHECKSUM_FAIL;
                last_error = vr.message;
                last_error_code = "VERIFY_FAILED";

                if (cfg.fail_fast) {
                    break;
                }
            }
        }

        frame_index++;

        heartbeat.MaybeEmit(
            logger,
            total_elapsed,
            "running",
            frame_index,
            gpu_job_count,
            last_frame_ms,
            last_gpu_ms
        );
    }

    const double actual_duration_s = NowSeconds() - run_start;

    StatSummary gpu_stats = metrics.GpuTimeStats();
    bool gpu_timestamp_valid =
        cfg.gpu_timestamp &&
        gpu_stats.avg > 0.0;

    backend->Destroy();

    if (verify_fail_count > 0 && final_result == ResultCode::PASS) {
        final_result = ResultCode::CHECKSUM_FAIL;
    }

    SummaryData summary;
    summary.result = final_result;
    summary.exit_code = ResultToExitCode(final_result);
    summary.config = cfg;

    summary.frame_count = metrics.FrameCount();
    summary.actual_duration_s = actual_duration_s;
    summary.actual_warmup_s = actual_warmup_s;

    summary.fps_avg =
        actual_duration_s > 0.0
            ? static_cast<double>(summary.frame_count) / actual_duration_s
            : 0.0;

    summary.fps_min = summary.fps_avg;
    summary.fps_max = summary.fps_avg;

    summary.frame_time = metrics.FrameTimeStats();

    summary.gpu_timestamp_valid = gpu_timestamp_valid;
    summary.gpu_job_time = gpu_stats;

    summary.verify_pass = verify_fail_count == 0;
    summary.verify_mode = cfg.verify_mode;
    summary.verify_fail_count = verify_fail_count;
    summary.first_fail_frame = first_fail_frame;
    summary.checksum = last_checksum;
    summary.golden_checksum = cfg.golden_checksum;
    summary.pixel_diff_count = pixel_diff_count;
    summary.compute_mismatch_count = compute_mismatch_count;

    summary.timeout_count = timeout_count;
    summary.api_error_count = api_error_count;
    summary.device_lost_count = device_lost_count;
    summary.allocation_fail_count = allocation_fail_count;

    summary.heartbeat_last_time_ms = logger.LastHeartbeatTimeMs();
    summary.last_error = last_error;
    summary.last_error_code = last_error_code;

    logger.EmitSummary(summary);

    return final_result;
}

} // namespace gpu_avs
