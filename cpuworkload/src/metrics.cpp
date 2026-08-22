#include "cpu_avs/metrics.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace cpu_avs {

void MetricsCollector::AddBatchTime(double ms) {
    batch_times_ms_.push_back(ms);
}

void MetricsCollector::AddThroughputSample(double operations_per_sec) {
    if (operations_per_sec >= 0.0) {
        throughput_samples_.push_back(operations_per_sec);
    }
}

uint64_t MetricsCollector::BatchCount() const {
    return static_cast<uint64_t>(batch_times_ms_.size());
}

StatSummary MetricsCollector::BatchTimeStats() const {
    return ComputeStats(batch_times_ms_);
}

StatSummary MetricsCollector::ThroughputStats() const {
    return ComputeStats(throughput_samples_);
}

StatSummary MetricsCollector::ComputeStats(std::vector<double> values) {
    StatSummary summary;
    if (values.empty()) {
        return summary;
    }

    std::sort(values.begin(), values.end());
    summary.count = static_cast<uint64_t>(values.size());
    const double sum = std::accumulate(values.begin(), values.end(), 0.0);
    summary.avg = sum / static_cast<double>(values.size());
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
    summary.stddev = std::sqrt(squared_sum / static_cast<double>(values.size()));
    summary.cv_pct = summary.avg != 0.0 ? (summary.stddev / summary.avg) * 100.0 : 0.0;
    return summary;
}

} // namespace cpu_avs
