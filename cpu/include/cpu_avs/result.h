#pragma once

namespace cpu_avs {

enum class ResultCode {
    PASS = 0,
    CHECKSUM_FAIL = 1,
    API_ERROR = 2,
    TIMEOUT = 3,
    DEVICE_LOST = 4,
    ALLOCATION_FAIL = 5,
    UNKNOWN_ERROR = 6,
    PERFORMANCE_FAIL = 7
};

inline const char* ResultToString(ResultCode result) {
    switch (result) {
        case ResultCode::PASS: return "PASS";
        case ResultCode::CHECKSUM_FAIL: return "CHECKSUM_FAIL";
        case ResultCode::API_ERROR: return "API_ERROR";
        case ResultCode::TIMEOUT: return "TIMEOUT";
        case ResultCode::DEVICE_LOST: return "DEVICE_LOST";
        case ResultCode::ALLOCATION_FAIL: return "ALLOCATION_FAIL";
        case ResultCode::PERFORMANCE_FAIL: return "PERFORMANCE_FAIL";
        case ResultCode::UNKNOWN_ERROR: return "UNKNOWN_ERROR";
        default: return "UNKNOWN_ERROR";
    }
}

inline int ResultToExitCode(ResultCode result) {
    return static_cast<int>(result);
}

enum class BackendStatus {
    Ok,
    Error,
    AllocationFail,
    UnknownError
};

inline ResultCode BackendStatusToResult(BackendStatus status) {
    switch (status) {
        case BackendStatus::Ok: return ResultCode::PASS;
        case BackendStatus::Error: return ResultCode::API_ERROR;
        case BackendStatus::AllocationFail: return ResultCode::ALLOCATION_FAIL;
        case BackendStatus::UnknownError: return ResultCode::UNKNOWN_ERROR;
        default: return ResultCode::UNKNOWN_ERROR;
    }
}

} // namespace cpu_avs
