#pragma once

#include <cstdint>
#include <vector>

namespace gpu_avs {

struct StatSummary {
    double avg = 0.0;
    double min = 0.0;
    double max = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
};

class MetricsCollector {
public:
    void AddFrameTime(double ms);
    void AddGpuTime(double ms);

    uint64_t FrameCount() const;

    StatSummary FrameTimeStats() const;
    StatSummary GpuTimeStats() const;

    static StatSummary ComputeStats(std::vector<double> values);

private:
    std::vector<double> frame_times_ms_;
    std::vector<double> gpu_times_ms_;
};

} // namespace gpu_avs
