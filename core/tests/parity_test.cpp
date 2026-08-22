#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <vector>

#include "fixture_loader.h"
#include "qx/ExecutionSimulator.h"
#include "qx/Recorder.h"
#include "qx/Replay.h"
#include "qx/SequenceValidator.h"

namespace {

std::string recordedFixturePath(const std::string& name)
{
    return (std::filesystem::temp_directory_path() / name).string();
}

std::vector<qx::MarketEvent> loadFixture(const std::string& name)
{
    return qx::test::loadEventsJsonl(std::string(QX_FIXTURE_DIR) + "/" + name);
}

std::string recordFixtureTo(const std::string& path, const std::vector<qx::MarketEvent>& events)
{
    qx::Recorder recorder(path);
    for (const auto& event : events) {
        REQUIRE(recorder.record(event));
    }
    return path;
}

// The live pipeline: validator decides, then the book applies. Mirrors
// FeedClient::handleFrame exactly, driven from in-memory events.
struct LiveStats {
    std::uint64_t applied = 0;
    std::uint64_t staleRejected = 0;
    std::uint64_t gapResyncs = 0;
};

LiveStats applyLive(qx::Book& book, const std::vector<qx::MarketEvent>& events)
{
    LiveStats stats;
    qx::SequenceValidator validator;

    for (const auto& event : events) {
        const auto verdict = validator.validate(event).verdict;
        switch (verdict) {
        case qx::SequenceValidator::Verdict::Accept:
            if (event.type == qx::EventType::Snapshot) {
                book.applySnapshot(event);
            } else {
                book.applyDelta(event);
            }
            ++stats.applied;
            break;

        case qx::SequenceValidator::Verdict::StaleReject:
            ++stats.staleRejected;
            break;

        case qx::SequenceValidator::Verdict::GapResync:
            book.clear();
            validator.reset();
            ++stats.gapResyncs;
            break;
        }
    }

    return stats;
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

void checkParity(const qx::Book& liveBook, const qx::Book& replayBook)
{
    CHECK(liveBook.serialize() == replayBook.serialize());
    CHECK(liveBook.bids() == replayBook.bids());
    CHECK(liveBook.asks() == replayBook.asks());
    CHECK(liveBook.lastSequence() == replayBook.lastSequence());

    const auto liveResults = fixedOrderResults(liveBook);
    const auto replayResults = fixedOrderResults(replayBook);

    REQUIRE(liveResults.size() == replayResults.size());
    bool allIdentical = true;
    for (std::size_t i = 0; i < liveResults.size(); ++i) {
        allIdentical = allIdentical && (liveResults[i] == replayResults[i]);
    }
    CHECK(allIdentical);
}

} // namespace

TEST_CASE("parity invariant on the BTC fixture: identical books and executions")
{
    const auto events = loadFixture("btc_snapshot_deltas.jsonl");
    const auto logPath = recordFixtureTo(recordedFixturePath("qx_parity_fixture.jsonl"), events);

    qx::Book liveBook;
    const auto live = applyLive(liveBook, events);

    qx::Book replayBook;
    const auto stats = qx::replayInto(replayBook, logPath);

    CHECK(live.applied == 4);
    CHECK(stats.eventsApplied == 4);
    CHECK(stats.gapResyncs == 0);
    CHECK(stats.malformedLines == 0);
    CHECK(stats.lastSequence == 19u);

    checkParity(liveBook, replayBook);

    { std::error_code ec; std::filesystem::remove(logPath, ec); }
}

TEST_CASE("parity invariant on the real-shape OKX chain: jumps, keepalive, maintenance reset")
{
    // Chain shape: snapshot(prevSeqId=-1) -> forward seqId jump -> empty
    // same-seq keepalive -> normal update -> maintenance reset to a LOWER
    // seqId -> post-reset update.
    const auto events = loadFixture("okx_parity_chain.jsonl");
    REQUIRE(events.size() == 6);
    CHECK(events[0].prevSequence.value_or(0) == -1);
    CHECK(events[1].sequence == 15u);
    CHECK(events[2].sequence == static_cast<std::uint64_t>(*events[2].prevSequence));
    CHECK(static_cast<std::int64_t>(events[4].sequence) < *events[4].prevSequence);

    const auto logPath = recordFixtureTo(recordedFixturePath("qx_parity_okx_chain.jsonl"), events);

    qx::Book liveBook;
    const auto live = applyLive(liveBook, events);

    qx::Book replayBook;
    const auto stats = qx::replayInto(replayBook, logPath);

    CHECK(live.applied == 6);
    CHECK(stats.eventsApplied == 6);
    CHECK(stats.gapResyncs == 0);
    CHECK(stats.lastSequence == 5u);

    checkParity(liveBook, replayBook);

    { std::error_code ec; std::filesystem::remove(logPath, ec); }
}

TEST_CASE("replay speed multiplier changes pacing only, never the outcome")
{
    const auto events = loadFixture("btc_snapshot_deltas.jsonl");
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

    { std::error_code ec; std::filesystem::remove(logPath, ec); }
}

TEST_CASE("corrupted replay breaks parity - the guard guards")
{
    const auto events = loadFixture("btc_snapshot_deltas.jsonl");
    REQUIRE(events.size() == 4);

    SUBCASE("a dropped delta changes the final book content")
    {
        std::vector<qx::MarketEvent> corrupted(events);
        corrupted.erase(corrupted.begin() + 3);

        const auto logPath = recordFixtureTo(
            recordedFixturePath("qx_parity_corrupt.jsonl"), corrupted);

        qx::Book liveBook;
        applyLive(liveBook, events);

        qx::Book replayBook;
        const auto stats = qx::replayInto(replayBook, logPath);
        CHECK(stats.eventsApplied == 3);
        CHECK(stats.gapResyncs == 0);

        CHECK_FALSE(liveBook.serialize() == replayBook.serialize());

        { std::error_code ec; std::filesystem::remove(logPath, ec); }
    }

    SUBCASE("a tampered prevSeqId breaks continuity and clears the replay book")
    {
        std::vector<qx::MarketEvent> corrupted(events);
        corrupted[3].prevSequence = 14;

        const auto logPath = recordFixtureTo(
            recordedFixturePath("qx_parity_corrupt_seq.jsonl"), corrupted);

        qx::Book liveBook;
        applyLive(liveBook, events);

        qx::Book replayBook;
        const auto stats = qx::replayInto(replayBook, logPath);
        CHECK(stats.gapResyncs >= 1);
        CHECK(replayBook.empty());

        CHECK_FALSE(liveBook.serialize() == replayBook.serialize());

        { std::error_code ec; std::filesystem::remove(logPath, ec); }
    }

    SUBCASE("divergence is visible in execution results too")
    {
        std::vector<qx::MarketEvent> corrupted(events);
        corrupted.erase(corrupted.begin() + 3);

        const auto logPath = recordFixtureTo(
            recordedFixturePath("qx_parity_corrupt_exec.jsonl"), corrupted);

        qx::Book liveBook;
        applyLive(liveBook, events);

        qx::Book replayBook;
        (void)qx::replayInto(replayBook, logPath);

        const auto liveResults = fixedOrderResults(liveBook);
        const auto replayResults = fixedOrderResults(replayBook);
        bool allIdentical = true;
        for (std::size_t i = 0; i < liveResults.size(); ++i) {
            allIdentical = allIdentical && (liveResults[i] == replayResults[i]);
        }
        CHECK_FALSE(allIdentical);

        { std::error_code ec; std::filesystem::remove(logPath, ec); }
    }
}