#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fixture_loader.h"
#include "qx/SequenceValidator.h"

TEST_CASE("crc32 matches the standard known-answer vector")
{
    CHECK(qx::crc32("123456789") == 0xCBF43926u);
    CHECK(qx::crc32("") == 0u);
}

TEST_CASE("consecutive sequences are accepted")
{
    qx::SequenceValidator validator;

    qx::MarketEvent snap;
    snap.type = qx::EventType::Snapshot;
    snap.sequence = 100;
    CHECK(validator.validate(snap).verdict == qx::SequenceValidator::Verdict::Accept);

    qx::MarketEvent delta;
    delta.sequence = 101;
    CHECK(validator.validate(delta).verdict == qx::SequenceValidator::Verdict::Accept);
    delta.sequence = 102;
    CHECK(validator.validate(delta).verdict == qx::SequenceValidator::Verdict::Accept);

    CHECK(validator.stats().accepted == 3);
    CHECK(validator.stats().staleRejected == 0);
    CHECK(validator.stats().gapResyncs == 0);
}

TEST_CASE("snapshot resets the sequence baseline after a gap")
{
    qx::SequenceValidator validator;

    qx::MarketEvent snap;
    snap.type = qx::EventType::Snapshot;
    snap.sequence = 10;
    (void)validator.validate(snap);

    qx::MarketEvent gap;
    gap.sequence = 50;
    CHECK(validator.validate(gap).verdict == qx::SequenceValidator::Verdict::GapResync);

    snap.sequence = 55;
    CHECK(validator.validate(snap).verdict == qx::SequenceValidator::Verdict::Accept);

    qx::MarketEvent delta;
    delta.sequence = 56;
    CHECK(validator.validate(delta).verdict == qx::SequenceValidator::Verdict::Accept);
}

TEST_CASE("gap fixture triggers exactly one resync signal")
{
    const auto events = qx::test::loadEventsJsonl(
        std::string(QX_FIXTURE_DIR) + "/gap_sequence.jsonl");
    REQUIRE(events.size() == 3);

    qx::SequenceValidator validator;
    std::vector<qx::SequenceValidator::Verdict> verdicts;
    for (const auto& event : events) {
        verdicts.push_back(validator.validate(event).verdict);
    }

    CHECK(verdicts[0] == qx::SequenceValidator::Verdict::Accept);
    CHECK(verdicts[1] == qx::SequenceValidator::Verdict::Accept);
    CHECK(verdicts[2] == qx::SequenceValidator::Verdict::GapResync);

    CHECK(validator.stats().gapResyncs == 1);
}

TEST_CASE("arrival mode stamps its own monotonic sequence")
{
    qx::SequenceValidator validator(qx::SequenceValidator::Mode::Arrival);

    std::uint64_t expectedSequence = 0;
    for (std::uint64_t raw : { 5u, 5u, 12u, 2u }) {
        qx::MarketEvent event;
        event.sequence = raw;
        const auto result = validator.validate(event);
        CHECK(result.verdict == qx::SequenceValidator::Verdict::Accept);
        CHECK(result.effectiveSequence == ++expectedSequence);
    }
}

TEST_CASE("checksum verification over the applied book")
{
    const auto events = qx::test::loadEventsJsonl(
        std::string(QX_FIXTURE_DIR) + "/btc_snapshot_deltas.jsonl");

    qx::Book book;
    book.applySnapshot(events[0]);

    const std::int32_t checksum = qx::computeTopOfBookChecksum(book);
    CHECK(checksum != 0);

    SUBCASE("verify passes with matching checksum")
    {
        qx::MarketEvent applied = events[0];
        applied.checksum = checksum;
        qx::SequenceValidator validator;
        CHECK(validator.verifyChecksum(book, applied));
        CHECK(validator.stats().checksumFailures == 0);
    }

    SUBCASE("tampered top-of-book fails verification")
    {
        qx::MarketEvent tampered = events[0];
        tampered.checksum = static_cast<std::int64_t>(checksum) + 1;
        qx::SequenceValidator validator;
        CHECK_FALSE(validator.verifyChecksum(book, tampered));
        CHECK(validator.stats().checksumFailures == 1);
    }

    SUBCASE("events without checksum pass through")
    {
        qx::MarketEvent noChecksum = events[0];
        noChecksum.checksum.reset();
        qx::SequenceValidator validator;
        CHECK(validator.verifyChecksum(book, noChecksum));
    }

    SUBCASE("checksum reflects book state")
    {
        qx::Book other;
        qx::MarketEvent modified = events[0];
        modified.bids[0].price += 0.25;
        other.applySnapshot(modified);
        CHECK(qx::computeTopOfBookChecksum(other) != checksum);
    }
}

TEST_CASE("reset clears counters and baseline")
{
    qx::SequenceValidator validator;

    qx::MarketEvent snap;
    snap.type = qx::EventType::Snapshot;
    snap.sequence = 5;
    (void)validator.validate(snap);

    qx::MarketEvent stale;
    stale.sequence = 1;
    (void)validator.validate(stale);
    CHECK(validator.stats().staleRejected == 1);

    validator.reset();
    CHECK(validator.stats().accepted == 0);
    CHECK(validator.stats().staleRejected == 0);

    qx::MarketEvent fresh;
    fresh.type = qx::EventType::Snapshot;
    fresh.sequence = 9;
    CHECK(validator.validate(fresh).verdict == qx::SequenceValidator::Verdict::Accept);
}
