#include "null_backend.h"

#include <algorithm>
#include <chrono>
#include <thread>

namespace npu_avs {
namespace {

bool WaitCancellable(uint32_t milliseconds, const std::atomic<bool>& cancelled) {
    for (uint32_t elapsed = 0; elapsed < milliseconds; ++elapsed) {
        if (cancelled.load(std::memory_order_relaxed)) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return !cancelled.load(std::memory_order_relaxed);
}

} // namespace

bool NullBackend::SupportsProfile(const ProfileSpec& profile) const {
    return profile.implemented && !profile.inputs.empty() && !profile.outputs.empty();
}

BackendStatus NullBackend::Init(const WorkloadConfig& cfg, const ProfileSpec& profile,
                                std::string& error) {
    if (!SupportsProfile(profile)) {
        error = "null backend does not support profile '" + profile.id + "'";
        return BackendStatus::Error;
    }
    Configure(cfg, profile);
    return BackendStatus::Ok;
}

BackendStatus NullBackend::CreateResources(std::string& error) {
    (void)error;
    return BackendStatus::Ok;
}

AsyncBackendBase::WorkTask NullBackend::MakeTask(const TensorSet& inputs,
                                                  uint64_t inference_index) const {
    (void)inputs;
    (void)inference_index;
    const uint32_t latency_ms = cfg_.simulated_latency_ms;
    const std::vector<TensorSpec> outputs = profile_.outputs;
    return [latency_ms, outputs](const std::atomic<bool>& cancelled) {
        WorkResult work;
        if (!WaitCancellable(latency_ms, cancelled)) {
            work.status = BackendStatus::Timeout;
            work.error = "null inference cancelled";
            return work;
        }
        for (const TensorSpec& spec : outputs) {
            uint64_t bytes = 0;
            std::string error;
            if (!TensorByteSize(spec, bytes, error)) {
                work.status = BackendStatus::Error;
                work.error = error;
                return work;
            }
            TensorBuffer output;
            output.spec = spec;
            output.data.resize(static_cast<size_t>(bytes), 0U);
            work.inference.operation_count += TensorElementCount(spec);
            work.inference.outputs.push_back(std::move(output));
        }
        return work;
    };
}

BackendExecutionInfo NullBackend::GetExecutionInfo() const {
    return {"null", "none", {profile_.id + ":null"}, false};
}

} // namespace npu_avs
