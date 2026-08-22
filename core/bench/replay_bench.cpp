#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>

#include "qx/ExecutionSimulator.h"
#include "qx/Metrics.h"
#include "qx/Recorder.h"
#include "qx/Replay.h"

namespace {

using Clock = std::chrono::steady_clock;

std::string benchLogPath()
{
    return (std::filesystem::temp_directory_path() / "qx_replay_bench.jsonl").string();
}

void report(const char* name, const qx::Histogram& histogram, double opsPerSecond)
{
    const auto percentiles = qx::summarize(histogram);
    std::printf("%-26s n=%-9zu mean=%-11s p50=%-11s p95=%-11s p99=%-11s", name,
        histogram.size(), qx::toHumanNs(histogram.mean()).c_str(),
        qx::toHumanNs(percentiles.p50).c_str(), qx::toHumanNs(percentiles.p95).c_str(),
        qx::toHumanNs(percentiles.p99).c_str());
    if (opsPerSecond > 0.0) {
        std::printf(" ops/s=%.0f", opsPerSecond);
    }
    std::printf("\n");
}

void generateRecording(const std::string& path)
{
    constexpr std::size_t kLevelsPerSide = 500;
    constexpr std::uint64_t kDeltaEvents = 100000;

    std::filesystem::remove(path);

    qx::Recorder recorder(path);
    if (!recorder.isOpen()) {
        std::fprintf(stderr, "cannot create %s\n", path.c_str());
        return;
    }

    qx::MarketEvent snapshot;
    snapshot.type = qx::EventType::Snapshot;
    snapshot.sequence = 1000000;
    snapshot.timestampNs = 1755850000000000000LL;
    for (std::size_t i = 0; i < kLevelsPerSide; ++i) {
        const double price = 100.0 + static_cast<double>(i) * 0.5;
        snapshot.bids.push_back({ price - 0.25, 2.0 });
        snapshot.asks.push_back({ price, 1.5 });
    }
    (void)recorder.record(snapshot);

    for (std::uint64_t i = 0; i < kDeltaEvents; ++i) {
        qx::MarketEvent delta;
        delta.sequence = snapshot.sequence + i + 1;
        delta.timestampNs = snapshot.timestampNs + static_cast<std::int64_t>(i) * 250000LL;
        const bool isBid = (i % 2u) == 0u;
        const double price
            = 100.0 - 50.0 + static_cast<double>(i % 10000) * 0.125 + (isBid ? -0.5 : 0.5);
        if (isBid) {
            delta.bids.push_back({ price, 0.5 });
        } else {
            delta.asks.push_back({ price, 0.5 });
        }
        (void)recorder.record(delta);
    }
}

} // namespace

int main(int argc, char** argv)
{
    const std::string path = argc > 1 ? argv[1] : benchLogPath();

    generateRecording(path);

    {
        qx::Book book;

        qx::Histogram applyHistogram;
        std::size_t applied = 0;
        const auto runStart = Clock::now();

        qx::EventLogReader reader(path);
        while (auto event = reader.next()) {
            const auto start = Clock::now();
            if (event->type == qx::EventType::Snapshot) {
                book.applySnapshot(*event);
            } else {
                book.applyDelta(*event);
            }
            ++applied;
            applyHistogram.observe(static_cast<double>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start)
                    .count()));
        }

        const double wallSec
            = std::chrono::duration<double>(Clock::now() - runStart).count();
        report("replay apply/event", applyHistogram,
            wallSec > 0.0 ? static_cast<double>(applied) / wallSec : 0.0);
        std::printf("%-26s events=%zu wall=%.3fs\n", "replay sustained", applied, wallSec);

        const qx::ExecutionSimulator simulator;
        const qx::OrderRequest request { qx::Side::Buy, qx::SizeMode::Notional, 50000.0, 5.0 };

        qx::Histogram executionHistogram;
        for (int i = 0; i < 10000; ++i) {
            const auto start = Clock::now();
            (void)simulator.execute(book, request);
            executionHistogram.observe(static_cast<double>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start)
                    .count()));
        }
        report("execute(replayed book)", executionHistogram,
            static_cast<double>(executionHistogram.size())
                / (executionHistogram.mean() * static_cast<double>(executionHistogram.size())
                      / 1e9));
    }

    std::printf("recording size: %.1f MB (%s)\n",
        static_cast<double>(std::filesystem::file_size(path)) / (1024.0 * 1024.0),
        path.c_str());

    return 0;
}
