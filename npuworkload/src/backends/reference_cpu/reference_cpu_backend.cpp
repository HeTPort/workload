#include "reference_cpu_backend.h"

#include "npu_avs/utils.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

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

bool WaitCancellable(uint32_t milliseconds, const std::atomic<bool>& cancelled) {
    for (uint32_t elapsed = 0; elapsed < milliseconds; ++elapsed) {
        if (cancelled.load(std::memory_order_relaxed)) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return !cancelled.load(std::memory_order_relaxed);
}

} // namespace

bool ReferenceCpuBackend::SupportsProfile(const ProfileSpec& profile) const {
    return profile.id == "framework_smoke" && profile.implemented;
}

BackendStatus ReferenceCpuBackend::Init(const WorkloadConfig& cfg, const ProfileSpec& profile,
                                        std::string& error) {
    if (!SupportsProfile(profile)) {
        error = "reference_cpu backend does not support profile '" + profile.id + "'";
        return BackendStatus::Error;
    }
    Configure(cfg, profile);
    return BackendStatus::Ok;
}

BackendStatus ReferenceCpuBackend::CreateResources(std::string& error) {
    (void)error;
    return BackendStatus::Ok;
}

AsyncBackendBase::WorkTask ReferenceCpuBackend::MakeTask(const TensorSet& inputs,
                                                          uint64_t inference_index) const {
    (void)inference_index;
    const uint64_t seed = cfg_.seed;
    const uint32_t latency_ms = cfg_.simulated_latency_ms;
    const std::vector<TensorSpec> outputs = profile_.outputs;
    return [seed, latency_ms, inputs, outputs](const std::atomic<bool>& cancelled) {
        WorkResult work;
        if (!WaitCancellable(latency_ms, cancelled)) {
            work.status = BackendStatus::Timeout;
            work.error = "reference inference cancelled";
            return work;
        }
        const double start = NowSeconds();
        const TensorBuffer& features = inputs.at(0);
        const TensorBuffer& context = inputs.at(1);

        TensorBuffer scores;
        scores.spec = outputs.at(0);
        scores.data.resize(static_cast<size_t>(TensorElementCount(scores.spec)));
        for (size_t out_index = 0; out_index < scores.data.size(); ++out_index) {
            if (cancelled.load(std::memory_order_relaxed)) {
                work.status = BackendStatus::Timeout;
                work.error = "reference inference cancelled";
                return work;
            }
            int64_t accumulator = static_cast<int32_t>(Mix64(seed + out_index) & 0x3ffU) - 512;
            for (size_t in_index = 0; in_index < features.data.size(); ++in_index) {
                const uint64_t key = seed ^ (static_cast<uint64_t>(out_index) << 32U) ^ in_index;
                const int32_t weight = static_cast<int32_t>(Mix64(key) % 15U) - 7;
                accumulator += AsInt8(features.data[in_index]) * weight;
            }
            accumulator += context.data[out_index % context.data.size()];
            const int64_t quantized = accumulator / 64;
            const int32_t clamped = static_cast<int32_t>(
                std::max<int64_t>(-128, std::min<int64_t>(127, quantized)));
            scores.data[out_index] = static_cast<uint8_t>(static_cast<int8_t>(clamped));
        }

        TensorBuffer diagnostics;
        diagnostics.spec = outputs.at(1);
        diagnostics.data.resize(sizeof(float) * 2U);
        const float values[2] = {
            static_cast<float>(features.data.size() + context.data.size()),
            static_cast<float>(scores.data.size())
        };
        std::memcpy(diagnostics.data.data(), values, sizeof(values));

        work.inference.outputs.push_back(std::move(scores));
        work.inference.outputs.push_back(std::move(diagnostics));
        work.inference.operation_count =
            static_cast<uint64_t>(features.data.size()) * outputs.at(0).shape.back() * 2ULL +
            outputs.at(0).shape.back();
        work.inference.device_time_ms = (NowSeconds() - start) * 1000.0;
        work.inference.device_time_valid = true;
        return work;
    };
}

BackendExecutionInfo ReferenceCpuBackend::GetExecutionInfo() const {
    return {"builtin_framework_reference", "cpu_reference",
            {profile_.id + ":cpu_reference"}, false};
}

} // namespace npu_avs
