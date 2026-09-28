#pragma once

#include "npu_avs/config.h"
#include "npu_avs/result.h"

namespace npu_avs {

ResultCode RunWorkload(const WorkloadConfig& cfg);

} // namespace npu_avs
