#include "qx/Metrics.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace qx {

void Histogram::observe(double value)
{
    samples_.push_back(value);
}

double Histogram::mean() const
{
    if (samples_.empty()) {
        return 0.0;
    }
    double total = 0.0;
    for (const double value : samples_) {
        total += value;
    }
    return total / static_cast<double>(samples_.size());
}

double Histogram::percentile(double p) const
{
    if (samples_.empty()) {
        return 0.0;
    }

    std::vector<double> sorted(samples_);
    std::sort(sorted.begin(), sorted.end());

    double rank = std::ceil(p / 100.0 * static_cast<double>(sorted.size()));
    rank = std::max(1.0, std::min(rank, static_cast<double>(sorted.size())));
    return sorted[static_cast<std::size_t>(rank) - 1];
}

Percentiles summarize(const Histogram& histogram)
{
    Percentiles result;
    result.p50 = histogram.percentile(50.0);
    result.p95 = histogram.percentile(95.0);
    result.p99 = histogram.percentile(99.0);
    return result;
}

std::string toHumanNs(double ns)
{
    std::ostringstream out;
    if (ns < 1'000.0) {
        out << ns << " ns";
    } else if (ns < 1'000'000.0) {
        out << ns / 1'000.0 << " us";
    } else if (ns < 1'000'000'000.0) {
        out << ns / 1'000'000.0 << " ms";
    } else {
        out << ns / 1'000'000'000.0 << " s";
    }
    return out.str();
}

} // namespace qx
