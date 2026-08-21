#pragma once

#include <string>

namespace gpu_avs {

enum class ResultCode {
    PASS = 0,
    CHECKSUM_FAIL = 1,
    API_ERROR = 2,
    TIMEOUT = 3,
    DEVICE_LOST = 4,
    ALLOCATION_FAIL = 5,
    UNKNOWN_ERROR = 6
};

inline const char* ResultToString(ResultCode r) {
    switch (r) {
        case ResultCode::PASS: return "PASS";
        case ResultCode::CHECKSUM_FAIL: return "CHECKSUM_FAIL";
        case ResultCode::API_ERROR: return "API_ERROR";
        case ResultCode::TIMEOUT: return "TIMEOUT";
        case ResultCode::DEVICE_LOST: return "DEVICE_LOST";
        case ResultCode::ALLOCATION_FAIL: return "ALLOCATION_FAIL";
        case ResultCode::UNKNOWN_ERROR: return "UNKNOWN_ERROR";
        default: return "UNKNOWN_ERROR";
    }
}

inline int ResultToExitCode(ResultCode r) {
    return static_cast<int>(r);
}

enum class SubmitStatus {
    Ok,
    ApiError,
    DeviceLost,
    GpuTimeout,
    AllocationFail,
    UnknownError
};

inline ResultCode SubmitStatusToResult(SubmitStatus s) {
    switch (s) {
        case SubmitStatus::Ok: return ResultCode::PASS;
        case SubmitStatus::ApiError: return ResultCode::API_ERROR;
        case SubmitStatus::DeviceLost: return ResultCode::DEVICE_LOST;
        case SubmitStatus::GpuTimeout: return ResultCode::TIMEOUT;
        case SubmitStatus::AllocationFail: return ResultCode::ALLOCATION_FAIL;
        case SubmitStatus::UnknownError: return ResultCode::UNKNOWN_ERROR;
        default: return ResultCode::UNKNOWN_ERROR;
    }
}

} // namespace gpu_avs
