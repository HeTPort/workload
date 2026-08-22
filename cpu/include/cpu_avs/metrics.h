#pragma once

#include <cstdint>
#include <vector>

namespace cpu_avs {

struct StatSummary {
    uint64_t count = 0;
    double avg = 0.0;
    double min = 0.0;
    double max = 0.0;
    double p05 = 0.0;
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double stddev = 0.0;
    double cv_pct = 0.0;
};

class MetricsCollector {
public:
    void AddBatchTime(double ms);
    void AddThroughputSample(double operations_per_sec);
    uint64_t BatchCount() const;
    StatSummary BatchTimeStats() const;
    StatSummary ThroughputStats() const;
    static StatSummary ComputeStats(std::vector<double> values);

private:
    std::vector<double> batch_times_ms_;
    std::vector<double> throughput_samples_;
};

} // namespace cpu_avs
