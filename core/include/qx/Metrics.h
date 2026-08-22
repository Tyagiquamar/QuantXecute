#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace qx {

class Histogram {
public:
    void observe(double value);

    std::size_t size() const { return samples_.size(); }
    bool empty() const { return samples_.empty(); }

    double mean() const;
    double percentile(double p) const;

private:
    std::vector<double> samples_;
};

struct Percentiles {
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
};

Percentiles summarize(const Histogram& histogram);

std::string toHumanNs(double ns);

struct EngineCounters {
    std::uint64_t framesReceived = 0;
    std::uint64_t snapshotsApplied = 0;
    std::uint64_t deltasApplied = 0;
    std::uint64_t staleRejected = 0;
    std::uint64_t gapsDetected = 0;
    std::uint64_t malformedFrames = 0;
    std::uint64_t checksumFailures = 0;
    std::uint64_t reconnects = 0;
    std::uint64_t executionsRun = 0;
};

} // namespace qx
