#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "fixture_loader.h"
#include "qx/Book.h"
#include "qx/SequenceValidator.h"

namespace {

qx::MarketEvent okxSnapshot(std::uint64_t seqId)
{
    qx::MarketEvent event;
    event.type = qx::EventType::Snapshot;
    event.sequence = seqId;
    event.hasSequence = true;
    event.prevSequence = -1;
    return event;
}

qx::MarketEvent okxDelta(std::uint64_t seqId, std::int64_t prevSeqId)
{
    qx::MarketEvent event;
    event.type = qx::EventType::Delta;
    event.sequence = seqId;
    event.hasSequence = true;
    event.prevSequence = prevSeqId;
    return event;
}

} // namespace

TEST_CASE("crc32 matches the standard known-answer vector")
{
    CHECK(qx::crc32("123456789") == 0xCBF43926u);
    CHECK(qx::crc32("") == 0u);
}

TEST_CASE("A: snapshot with prevSeqId=-1 establishes the baseline")
{
    qx::SequenceValidator validator;

    const auto result = validator.validate(okxSnapshot(10));
    CHECK(result.verdict == qx::SequenceValidator::Verdict::Accept);
    CHECK(result.effectiveSequence == 10u);
    CHECK(validator.stats().accepted == 1);
    CHECK(validator.stats().gapResyncs == 0);
}

TEST_CASE("B: forward seqId jump is accepted - continuity is NOT +1")
{
    qx::SequenceValidator validator;

    (void)validator.validate(okxSnapshot(10));

    auto jump = okxDelta(15, 10);
    jump.bids.push_back({ 100.0, 1.0 });
    const auto result = validator.validate(jump);
    CHECK(result.verdict == qx::SequenceValidator::Verdict::Accept);
    CHECK(result.effectiveSequence == 15u);
    CHECK(validator.stats().gapResyncs == 0);
}

TEST_CASE("C: normal next update chains prevSeqId to last accepted seqId")
{
    qx::SequenceValidator validator;

    (void)validator.validate(okxSnapshot(10));
    (void)validator.validate(okxDelta(15, 10));

    auto next = okxDelta(19, 15);
    next.asks.push_back({ 101.0, 0.5 });
    CHECK(validator.validate(next).verdict == qx::SequenceValidator::Verdict::Accept);
    CHECK(validator.stats().accepted == 3);
}

TEST_CASE("D: no-change keepalive stays valid and leaves the book untouched")
{
    qx::SequenceValidator validator;
    qx::Book book;

    auto snap = okxSnapshot(19);
    snap.bids.push_back({ 100.0, 2.0 });
    snap.asks.push_back({ 101.0, 3.0 });
    book.applySnapshot(snap);
    (void)validator.validate(snap);

    const std::string before = book.serialize();

    // Real OKX keepalive shape: asks=[], bids=[], prevSeqId == seqId == last.
    auto keepalive = okxDelta(19, 19);
    REQUIRE(keepalive.bids.empty());
    REQUIRE(keepalive.asks.empty());

    const auto result = validator.validate(keepalive);
    CHECK(result.verdict == qx::SequenceValidator::Verdict::Accept);

    book.applyDelta(keepalive);
    CHECK(validator.hasBaseline());
    CHECK(book.serialize() == before);
    CHECK(book.lastSequence() == 19u);
    CHECK(validator.stats().staleRejected == 0);
    CHECK(validator.stats().gapResyncs == 0);
}

TEST_CASE("E: real gap - prevSeqId mismatch invalidates and demands resync")
{
    qx::SequenceValidator validator;

    (void)validator.validate(okxSnapshot(10));
    (void)validator.validate(okxDelta(15, 10));
    (void)validator.validate(okxDelta(19, 15));

    // Last accepted seqId is 19; this message claims prevSeqId=18.
    auto gapped = okxDelta(25, 18);
    gapped.bids.push_back({ 99.0, 1.0 });

    const auto result = validator.validate(gapped);
    CHECK(result.verdict == qx::SequenceValidator::Verdict::GapResync);
    CHECK(validator.stats().gapResyncs == 1);
    CHECK(validator.stats().accepted == 3);
}

TEST_CASE("F: maintenance reset accepts a LOWER seqId when prevSeqId connects")
{
    qx::SequenceValidator validator;

    (void)validator.validate(okxSnapshot(25));

    auto reset = okxDelta(3, 25);
    reset.asks.push_back({ 50.0, 1.0 });
    CHECK(validator.validate(reset).verdict == qx::SequenceValidator::Verdict::Accept);

    auto postReset = okxDelta(5, 3);
    postReset.bids.push_back({ 49.0, 1.0 });
    CHECK(validator.validate(postReset).verdict == qx::SequenceValidator::Verdict::Accept);

    CHECK(validator.stats().accepted == 3);
    CHECK(validator.stats().gapResyncs == 0);
}

TEST_CASE("G: stale/out-of-order message whose prevSeqId misses the baseline rejects and resyncs")
{
    qx::SequenceValidator validator;

    (void)validator.validate(okxSnapshot(100));
    (void)validator.validate(okxDelta(110, 100));

    SUBCASE("older message replayed from a lagging stream")
    {
        auto late = okxDelta(112, 105);
        CHECK(validator.validate(late).verdict == qx::SequenceValidator::Verdict::GapResync);
        CHECK(validator.stats().gapResyncs == 1);
    }

    SUBCASE("duplicate seqId carrying levels is stale, not applicable twice")
    {
        auto duplicate = okxDelta(110, 110);
        duplicate.asks.push_back({ 100.0, 9.0 });
        CHECK(validator.validate(duplicate).verdict
            == qx::SequenceValidator::Verdict::StaleReject);
        CHECK(validator.stats().staleRejected == 1);
        CHECK(validator.stats().gapResyncs == 0);
    }
}

TEST_CASE("snapshot with malformed metadata (prevSeqId != -1) cannot anchor")
{
    qx::SequenceValidator validator;

    auto badSnap = okxSnapshot(42);
    badSnap.prevSequence = 41;
    const auto result = validator.validate(badSnap);
    CHECK(result.verdict == qx::SequenceValidator::Verdict::GapResync);
    CHECK_FALSE(validator.hasBaseline());
}

TEST_CASE("deltas before any snapshot are dropped without counting a feed gap")
{
    qx::SequenceValidator validator;

    auto early = okxDelta(500, 499);
    early.bids.push_back({ 10.0, 1.0 });
    CHECK(validator.validate(early).verdict == qx::SequenceValidator::Verdict::GapResync);
    CHECK(validator.stats().gapResyncs == 0);
    CHECK_FALSE(validator.hasBaseline());

    // The eventual snapshot still anchors cleanly.
    CHECK(validator.validate(okxSnapshot(501)).verdict
        == qx::SequenceValidator::Verdict::Accept);
}

TEST_CASE("delta without sequencing metadata cannot pass strict continuity")
{
    qx::SequenceValidator validator;

    (void)validator.validate(okxSnapshot(7));

    qx::MarketEvent unsequenced;
    unsequenced.type = qx::EventType::Delta;
    unsequenced.hasSequence = false;
    unsequenced.prevSequence = std::nullopt;
    unsequenced.bids.push_back({ 100.0, 1.0 });

    CHECK(validator.validate(unsequenced).verdict == qx::SequenceValidator::Verdict::GapResync);
}

TEST_CASE("arrival mode stamps its own monotonic sequence")
{
    qx::SequenceValidator validator(qx::SequenceValidator::Mode::Arrival);

    std::uint64_t expectedSequence = 0;
    for (std::uint64_t raw : { 5u, 5u, 12u, 2u }) {
        qx::MarketEvent event;
        event.sequence = raw;
        event.hasSequence = true;
        const auto result = validator.validate(event);
        CHECK(result.verdict == qx::SequenceValidator::Verdict::Accept);
        ++expectedSequence;
        CHECK(result.effectiveSequence == expectedSequence);
    }
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

TEST_CASE("checksum policy: current OKX books never verify the deprecated field")
{
    const auto events = qx::test::loadEventsJsonl(
        std::string(QX_FIXTURE_DIR) + "/btc_snapshot_deltas.jsonl");

    qx::Book book;
    book.applySnapshot(events[0]);

    qx::MarketEvent applied = events[0];

    SUBCASE("SequenceOnly ignores checksum=0 (deprecated on live OKX)")
    {
        applied.checksum = 0;
        qx::SequenceValidator validator;
        CHECK(validator.verifyChecksum(book, applied, qx::IntegrityPolicy::SequenceOnly)
            == qx::SequenceValidator::ChecksumVerdict::NotPresent);
        CHECK(validator.stats().checksumFailures == 0);
    }

    SUBCASE("SequenceOnly ignores even an intentionally wrong checksum")
    {
        applied.checksum = 123456789;
        qx::SequenceValidator validator;
        CHECK(validator.verifyChecksum(book, applied, qx::IntegrityPolicy::SequenceOnly)
            == qx::SequenceValidator::ChecksumVerdict::NotPresent);
        CHECK(validator.stats().checksumFailures == 0);
    }
}

TEST_CASE("checksum verification over the applied book when genuinely enabled")
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
        CHECK(validator.verifyChecksum(book, applied, qx::IntegrityPolicy::SequenceAndChecksum)
            == qx::SequenceValidator::ChecksumVerdict::Pass);
        CHECK(validator.stats().checksumFailures == 0);
    }

    SUBCASE("tampered top-of-book fails verification")
    {
        qx::MarketEvent tampered = events[0];
        tampered.checksum = static_cast<std::int64_t>(checksum) + 1;
        qx::SequenceValidator validator;
        CHECK(validator.verifyChecksum(book, tampered, qx::IntegrityPolicy::SequenceAndChecksum)
            == qx::SequenceValidator::ChecksumVerdict::Mismatch);
        CHECK(validator.stats().checksumFailures == 1);
    }

    SUBCASE("events without checksum pass through")
    {
        qx::MarketEvent noChecksum = events[0];
        noChecksum.checksum.reset();
        qx::SequenceValidator validator;
        CHECK(validator.verifyChecksum(book, noChecksum, qx::IntegrityPolicy::SequenceAndChecksum)
            == qx::SequenceValidator::ChecksumVerdict::NotPresent);
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

    (void)validator.validate(okxSnapshot(5));

    (void)validator.validate(okxDelta(6, 4));
    CHECK(validator.stats().gapResyncs == 1);

    validator.reset();
    CHECK(validator.stats().accepted == 0);
    CHECK(validator.stats().gapResyncs == 0);
    CHECK_FALSE(validator.hasBaseline());
    CHECK(validator.validate(okxSnapshot(9)).verdict
        == qx::SequenceValidator::Verdict::Accept);
}