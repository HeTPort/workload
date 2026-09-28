#include "npu_avs/metrics.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace npu_avs {

void MetricsCollector::AddHostInferenceTime(double ms) { host_times_ms_.push_back(ms); }
void MetricsCollector::AddDeviceInferenceTime(double ms) { device_times_ms_.push_back(ms); }
void MetricsCollector::AddThroughputSample(double value) {
    if (value >= 0.0) throughput_samples_.push_back(value);
}
StatSummary MetricsCollector::HostInferenceTimeStats() const { return ComputeStats(host_times_ms_); }
StatSummary MetricsCollector::DeviceInferenceTimeStats() const { return ComputeStats(device_times_ms_); }
StatSummary MetricsCollector::ThroughputStats() const { return ComputeStats(throughput_samples_); }

StatSummary MetricsCollector::ComputeStats(std::vector<double> values) {
    StatSummary summary;
    if (values.empty()) return summary;
    std::sort(values.begin(), values.end());
    summary.count = static_cast<uint64_t>(values.size());
    summary.avg = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
    summary.min = values.front();
    summary.max = values.back();
    const auto percentile = [&values](double p) {
        const double position = p * static_cast<double>(values.size() - 1U);
        const size_t lower = static_cast<size_t>(position);
        const size_t upper = std::min(lower + 1U, values.size() - 1U);
        const double fraction = position - static_cast<double>(lower);
        return values[lower] * (1.0 - fraction) + values[upper] * fraction;
    };
    summary.p05 = percentile(0.05);
    summary.p50 = percentile(0.50);
    summary.p95 = percentile(0.95);
    summary.p99 = percentile(0.99);
    double squared_sum = 0.0;
    for (double value : values) {
        const double delta = value - summary.avg;
        squared_sum += delta * delta;
    }
    summary.stddev = std::sqrt(squared_sum / values.size());
    summary.cv_pct = summary.avg != 0.0 ? summary.stddev * 100.0 / summary.avg : 0.0;
    return summary;
}

} // namespace npu_avs
