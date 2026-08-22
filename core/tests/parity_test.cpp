#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <filesystem>
#include <vector>

#include "fixture_loader.h"
#include "qx/ExecutionSimulator.h"
#include "qx/Recorder.h"
#include "qx/Replay.h"

namespace {

std::string recordedFixturePath(const std::string& name)
{
    return (std::filesystem::temp_directory_path() / name).string();
}

std::vector<qx::MarketEvent> loadBtcFixture()
{
    return qx::test::loadEventsJsonl(
        std::string(QX_FIXTURE_DIR) + "/btc_snapshot_deltas.jsonl");
}

std::string recordFixtureTo(const std::string& path, const std::vector<qx::MarketEvent>& events)
{
    qx::Recorder recorder(path);
    for (const auto& event : events) {
        REQUIRE(recorder.record(event));
    }
    return path;
}

void applyLive(qx::Book& book, const std::vector<qx::MarketEvent>& events)
{
    for (const auto& event : events) {
        if (event.type == qx::EventType::Snapshot) {
            book.applySnapshot(event);
        } else {
            book.applyDelta(event);
        }
    }
}

std::vector<qx::ExecutionResult> fixedOrderResults(const qx::Book& book)
{
    const qx::ExecutionSimulator simulator;

    std::vector<qx::ExecutionResult> results;
    results.push_back(simulator.execute(book,
        { qx::Side::Buy, qx::SizeMode::BaseQuantity, 1000.0, 5.0 }));
    results.push_back(simulator.execute(book,
        { qx::Side::Sell, qx::SizeMode::BaseQuantity, 500.0, 5.0 }));
    results.push_back(simulator.execute(book,
        { qx::Side::Buy, qx::SizeMode::Notional, 25000.0, 2.5 }));
    return results;
}

} // namespace

TEST_CASE("parity invariant: live-applied and replayed books are identical")
{
    const auto events = loadBtcFixture();
    const auto logPath = recordFixtureTo(recordedFixturePath("qx_parity_fixture.jsonl"), events);

    qx::Book liveBook;
    applyLive(liveBook, events);

    qx::Book replayBook;
    const auto stats = qx::replayInto(replayBook, logPath);

    CHECK(stats.eventsApplied == 4);
    CHECK(stats.staleRejected == 0);
    CHECK(stats.malformedLines == 0);
    CHECK(stats.lastSequence == 1003);

    CHECK(liveBook.serialize() == replayBook.serialize());
    CHECK(liveBook.bids() == replayBook.bids());
    CHECK(liveBook.asks() == replayBook.asks());
    CHECK(liveBook.lastSequence() == replayBook.lastSequence());

    SUBCASE("execution results are identical across both paths")
    {
        const auto liveResults = fixedOrderResults(liveBook);
        const auto replayResults = fixedOrderResults(replayBook);

        REQUIRE(liveResults.size() == replayResults.size());
        for (std::size_t i = 0; i < liveResults.size(); ++i) {
            CHECK(liveResults[i] == replayResults[i]);
        }
    }

    std::filesystem::remove(logPath);
}

TEST_CASE("replay speed multiplier changes pacing only, never the outcome")
{
    const auto events = loadBtcFixture();
    const auto logPath = recordFixtureTo(recordedFixturePath("qx_parity_speed.jsonl"), events);

    qx::Book referenceBook;
    applyLive(referenceBook, events);
    const auto referenceSerialize = referenceBook.serialize();

    for (const double speed : { 1.0, 10.0, 100.0 }) {
        qx::Book pacedBook;
        std::vector<std::int64_t> sleeps;
        qx::ReplayOptions options;
        options.speedMultiplier = speed;
        options.pace = [&sleeps](std::int64_t ns) { sleeps.push_back(ns); };

        const auto stats = qx::replayInto(pacedBook, logPath, options);

        CHECK(stats.eventsApplied == 4);
        CHECK(pacedBook.serialize() == referenceSerialize);
    }

    std::filesystem::remove(logPath);
}

TEST_CASE("corrupted replay breaks parity - the guard guards")
{
    const auto events = loadBtcFixture();
    REQUIRE(events.size() >= 3);

    std::vector<qx::MarketEvent> corrupted(events);
    corrupted.erase(corrupted.begin() + 2);

    const auto logPath = recordFixtureTo(recordedFixturePath("qx_parity_corrupt.jsonl"), corrupted);

    qx::Book liveBook;
    applyLive(liveBook, events);

    qx::Book replayBook;
    (void)qx::replayInto(replayBook, logPath);

    CHECK_FALSE(liveBook.serialize() == replayBook.serialize());

    const auto liveResults = fixedOrderResults(liveBook);
    const auto replayResults = fixedOrderResults(replayBook);
    bool allIdentical = true;
    for (std::size_t i = 0; i < liveResults.size(); ++i) {
        allIdentical = allIdentical && (liveResults[i] == replayResults[i]);
    }
    CHECK_FALSE(allIdentical);

    std::filesystem::remove(logPath);
}
