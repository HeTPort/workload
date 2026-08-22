#include "gpu_avs/metrics.h"

#include <algorithm>
#include <numeric>

namespace gpu_avs {

void MetricsCollector::AddFrameTime(double ms) {
    frame_times_ms_.push_back(ms);
}

void MetricsCollector::AddGpuTime(double ms) {
    gpu_times_ms_.push_back(ms);
}

uint64_t MetricsCollector::FrameCount() const {
    return static_cast<uint64_t>(frame_times_ms_.size());
}

StatSummary MetricsCollector::FrameTimeStats() const {
    return ComputeStats(frame_times_ms_);
}

StatSummary MetricsCollector::GpuTimeStats() const {
    return ComputeStats(gpu_times_ms_);
}

StatSummary MetricsCollector::ComputeStats(std::vector<double> values) {
    StatSummary s;

    if (values.empty()) {
        return s;
    }

    std::sort(values.begin(), values.end());

    const double sum = std::accumulate(values.begin(), values.end(), 0.0);
    s.avg = sum / static_cast<double>(values.size());
    s.min = values.front();
    s.max = values.back();

    auto percentile = [&](double p) -> double {
        if (values.empty()) {
            return 0.0;
        }

        double idx = p * static_cast<double>(values.size() - 1);
        size_t lo = static_cast<size_t>(idx);
        size_t hi = std::min(lo + 1, values.size() - 1);
        double frac = idx - static_cast<double>(lo);

        return values[lo] * (1.0 - frac) + values[hi] * frac;
    };

    s.p95 = percentile(0.95);
    s.p99 = percentile(0.99);

    return s;
}

} // namespace gpu_avs
