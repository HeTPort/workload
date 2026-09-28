#include "reference_cpu_backend.h"

#include "npu_avs/utils.h"

#include <algorithm>
#include <cstdint>

namespace npu_avs {
namespace {

uint64_t Mix64(uint64_t value) {
    value ^= value >> 30U;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27U;
    value *= 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

int32_t AsInt8(uint8_t value) {
    return value < 128U ? static_cast<int32_t>(value) : static_cast<int32_t>(value) - 256;
}

} // namespace

bool ReferenceCpuBackend::Init(const WorkloadConfig& cfg, std::string& error) {
    (void)error;
    Configure(cfg);
    return true;
}

bool ReferenceCpuBackend::CreateResources(std::string& error) {
    (void)error;
    return true;
}

AsyncBackendBase::WorkResult ReferenceCpuBackend::Execute(const TensorBuffer& input,
                                                           uint64_t inference_index) {
    (void)inference_index;
    if (cfg_.simulated_latency_ms > 0) SleepMs(cfg_.simulated_latency_ms);
    const double start = NowSeconds();

    WorkResult work;
    TensorBuffer& output = work.inference.output;
    output.name = "synthetic_dense_output";
    output.data_type = TensorDataType::Int8;
    output.shape = {1U, cfg_.output_elements};
    output.scale = 1.0F / 32.0F;
    output.zero_point = 0;
    output.data.resize(cfg_.output_elements);

    for (uint32_t out_index = 0; out_index < cfg_.output_elements; ++out_index) {
        int64_t accumulator = static_cast<int32_t>(Mix64(cfg_.seed + out_index) & 0x3ffU) - 512;
        for (size_t in_index = 0; in_index < input.data.size(); ++in_index) {
            const uint64_t key = cfg_.seed ^ (static_cast<uint64_t>(out_index) << 32U) ^ in_index;
            const int32_t weight = static_cast<int32_t>(Mix64(key) % 15U) - 7;
            accumulator += AsInt8(input.data[in_index]) * weight;
        }
        const int64_t quantized = accumulator / 64;
        const int32_t clamped = static_cast<int32_t>(std::max<int64_t>(-128, std::min<int64_t>(127, quantized)));
        output.data[out_index] = static_cast<uint8_t>(static_cast<int8_t>(clamped));
    }

    work.inference.operation_count =
        static_cast<uint64_t>(cfg_.input_elements) * cfg_.output_elements * 2ULL + cfg_.output_elements;
    work.inference.device_time_ms = (NowSeconds() - start) * 1000.0;
    work.inference.device_time_valid = true;
    return work;
}

BackendExecutionInfo ReferenceCpuBackend::GetExecutionInfo() const {
    return {"builtin_reference", "cpu_reference", {"synthetic_dense:cpu_reference"}, false};
}

} // namespace npu_avs
