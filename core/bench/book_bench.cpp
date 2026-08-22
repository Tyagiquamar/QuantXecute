#include <chrono>
#include <cstdio>
#include <cstdint>
#include <vector>

#include "qx/Book.h"
#include "qx/ExecutionSimulator.h"
#include "qx/Metrics.h"

namespace {

using Clock = std::chrono::steady_clock;

std::int64_t elapsedNs(Clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
}

std::uint32_t lcg(std::uint32_t& state)
{
    state = state * 1664525u + 1013904223u;
    return state;
}

void report(const char* name, const qx::Histogram& histogram)
{
    const auto percentiles = qx::summarize(histogram);
    std::printf("%-26s n=%-9zu mean=%-11s p50=%-11s p95=%-11s p99=%-11s ops/s=%.0f\n",
        name, histogram.size(), qx::toHumanNs(histogram.mean()).c_str(),
        qx::toHumanNs(percentiles.p50).c_str(), qx::toHumanNs(percentiles.p95).c_str(),
        qx::toHumanNs(percentiles.p99).c_str(),
        static_cast<double>(histogram.size())
            / (histogram.mean() * static_cast<double>(histogram.size()) / 1e9));
}

} // namespace

int main()
{
    constexpr std::size_t kLevelsPerSide = 5000;
    constexpr int kDeltaIterations = 200000;
    constexpr int kExecutionRuns = 50000;

    qx::Book book;

    {
        std::vector<qx::Level> bids;
        std::vector<qx::Level> asks;
        bids.reserve(kLevelsPerSide);
        asks.reserve(kLevelsPerSide);
        for (std::size_t i = 0; i < kLevelsPerSide; ++i) {
            bids.push_back({ 100.0 - static_cast<double>(i) * 0.5,
                1.0 + static_cast<double>(i % 10) * 0.1 });
            asks.push_back({ 100.5 + static_cast<double>(i) * 0.5,
                1.0 + static_cast<double>(i % 10) * 0.1 });
        }

        qx::MarketEvent snapshot;
        snapshot.type = qx::EventType::Snapshot;
        snapshot.sequence = 1;
        snapshot.bids = bids;
        snapshot.asks = asks;

        const auto start = Clock::now();
        book.applySnapshot(snapshot);
        report("applySnapshot(5000/side)", [&] {
            qx::Histogram h;
            h.observe(static_cast<double>(elapsedNs(start)));
            return h;
        }());
    }

    {
        qx::Histogram histogram;
        std::uint32_t rngState = 42u;

        for (int i = 0; i < kDeltaIterations; ++i) {
            const bool isBid = (lcg(rngState) & 1u) != 0u;
            const double price
                = 100.0 - static_cast<double>(lcg(rngState) % kLevelsPerSide) * 0.5;
            const double size = static_cast<double>(lcg(rngState) % 100) / 10.0;

            qx::MarketEvent delta;
            delta.sequence = static_cast<std::uint64_t>(i) + 2;
            if (isBid) {
                delta.bids.push_back({ price, size });
            } else {
                delta.asks.push_back({ price + 0.5, size });
            }

            const auto start = Clock::now();
            (void)book.applyDelta(delta);
            histogram.observe(static_cast<double>(elapsedNs(start)));
        }
        report("applyDelta", histogram);
    }

    {
        const qx::ExecutionSimulator simulator;
        const qx::OrderRequest request { qx::Side::Buy, qx::SizeMode::Notional, 250000.0, 5.0 };

        qx::Histogram histogram;
        for (int i = 0; i < kExecutionRuns; ++i) {
            const auto start = Clock::now();
            const auto result = simulator.execute(book, request);
            histogram.observe(static_cast<double>(elapsedNs(start)));

            if (i == 0 && result.insufficientLiquidity) {
                std::printf("warning: benchmark execution flagged insufficient liquidity\n");
            }
        }
        report("execute(notional)", histogram);
    }

    {
        qx::Histogram histogram;
        for (int i = 0; i < 200; ++i) {
            const auto start = Clock::now();
            const std::string text = book.serialize();
            histogram.observe(static_cast<double>(elapsedNs(start)));
            if (i == 0) {
                std::printf("serialize output: %zu bytes\n", text.size());
            }
        }
        report("serialize", histogram);
    }

    return 0;
}
