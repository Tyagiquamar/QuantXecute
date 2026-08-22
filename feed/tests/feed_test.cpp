#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "MockFeedSource.h"
#include "qx/feed/FeedClient.h"

namespace {

// Real-shape OKX v5 books frames: seqId/prevSeqId sequencing, string numbers,
// millisecond ts. No synthetic "seq" fields anywhere.

std::string okxSnapshot(std::uint64_t seqId)
{
    return R"({"arg":{"channel":"books","instId":"BTC-USDT"},"action":"snapshot",)"
        R"("data":[{"seqId":)" + std::to_string(seqId) + R"(,"prevSeqId":-1,)"
        R"("bids":[["100.0","5.0"],["99.0","3.0"]],"asks":[["101.0","4.0"]],)"
        R"("ts":"1755850000000"}]})";
}

std::string okxBidUpdate(std::uint64_t seqId, std::int64_t prevSeqId,
    const std::string& price, const std::string& size)
{
    return R"({"action":"update","data":[{"seqId":)" + std::to_string(seqId)
        + R"(,"prevSeqId":)" + std::to_string(prevSeqId)
        + R"(,"bids":[[")" + price + R"(",")" + size + R"("]],"asks":[],)"
        R"("ts":"1755850001000"}]})";
}

std::string okxKeepalive(std::uint64_t seqId)
{
    // Real no-change form: asks=[], bids=[], prevSeqId == seqId == last.
    return R"({"action":"update","data":[{"seqId":)" + std::to_string(seqId)
        + R"(,"prevSeqId":)" + std::to_string(seqId)
        + R"(,"bids":[],"asks":[],"ts":"1755850002000"}]})";
}

struct TestClock {
    std::int64_t nowNs = 1'000'000'000;

    std::int64_t operator()() const { return nowNs; }
};

qx::feed::FeedConfig testConfig(std::chrono::milliseconds staleness)
{
    qx::feed::FeedConfig config;
    config.stalenessThreshold = staleness;
    config.reconnectPolicy = qx::feed::ReconnectPolicy(
        std::chrono::milliseconds(100), std::chrono::milliseconds(5000), 0.0);
    return config;
}

} // namespace

TEST_CASE("disconnect schedules backoff reconnect, then resubscribes")
{
    qx::feed::test::MockFeedSource source;
    qx::feed::FeedClient client(source, testConfig(std::chrono::milliseconds(5000)));

    TestClock clock;
    client.setClock([&clock] { return clock.nowNs; });

    client.start();
    CHECK(client.health().state == qx::feed::ConnectionState::Connected);
    CHECK(source.connectCalls == 1);
    CHECK(source.resubscribeCalls == 1);
    CHECK(client.health().reconnects == 0);

    source.deliver(okxSnapshot(100));
    REQUIRE(client.isBookReady());

    source.remoteDisconnect();

    auto health = client.health();
    CHECK(health.state == qx::feed::ConnectionState::Disconnected);
    CHECK(source.connectCalls == 1);

    clock.nowNs += 50'000'000;
    client.tick();
    CHECK(client.health().state == qx::feed::ConnectionState::Disconnected);
    CHECK(source.connectCalls == 1);

    clock.nowNs += 60'000'000;
    client.tick();
    CHECK(client.health().state == qx::feed::ConnectionState::Connected);
    CHECK(source.connectCalls == 2);

    health = client.health();
    CHECK(health.reconnects == 1);
    CHECK(source.resubscribeCalls == 2);
}

TEST_CASE("reconnect delays grow exponentially and are capped")
{
    const qx::feed::ReconnectPolicy policy(
        std::chrono::milliseconds(100), std::chrono::milliseconds(5000), 0.0);

    CHECK(policy.delayForAttempt(0) == std::chrono::milliseconds(100));
    CHECK(policy.delayForAttempt(1) == std::chrono::milliseconds(200));
    CHECK(policy.delayForAttempt(2) == std::chrono::milliseconds(400));
    CHECK(policy.delayForAttempt(3) == std::chrono::milliseconds(800));
    CHECK(policy.delayForAttempt(10) == std::chrono::milliseconds(5000));
}

TEST_CASE("forward seqId jumps flow through the whole pipeline without false gaps")
{
    qx::feed::test::MockFeedSource source;
    qx::feed::FeedClient client(source, testConfig(std::chrono::milliseconds(5000)));

    TestClock clock;
    client.setClock([&clock] { return clock.nowNs; });
    client.start();

    const int resubscribesAfterStart = source.resubscribeCalls;

    // Baseline 100 -> jump to 105 -> jump to 112. None of these are +1.
    source.deliver(okxSnapshot(100));
    source.deliver(okxBidUpdate(105, 100, "100.5", "1.0"));
    source.deliver(okxBidUpdate(112, 105, "99.5", "2.0"));

    CHECK(client.book().lastSequence() == 112);
    CHECK(client.health().sequenceGaps == 0);
    CHECK(client.health().messagesAccepted == 3);
    CHECK(source.resubscribeCalls == resubscribesAfterStart);
    REQUIRE_FALSE(client.book().empty());
    REQUIRE_FALSE(client.book().bidLevels.empty());
    CHECK(client.book().bidLevels.front().price == doctest::Approx(100.5));
    CHECK(client.book().bidLevels.front().size == doctest::Approx(1.0));
}

TEST_CASE("no-change keepalive refreshes liveness, leaves the book untouched")
{
    qx::feed::test::MockFeedSource source;
    qx::feed::FeedClient client(source, testConfig(std::chrono::milliseconds(1000)));

    TestClock clock;
    client.setClock([&clock] { return clock.nowNs; });
    client.start();

    source.deliver(okxSnapshot(19));
    REQUIRE(client.isBookReady());
    const auto bookBefore = client.book();

    // Drive close to the staleness edge.
    clock.nowNs += 900'000'000;
    client.tick();
    CHECK_FALSE(client.health().stale);
    clock.nowNs += 200'000'000;
    client.tick();
    CHECK(client.health().stale);

    const int resubscribesBefore = source.resubscribeCalls;

    // Keepalive: asks=[], bids=[], prevSeqId == seqId == 19.
    source.deliver(okxKeepalive(19));

    CHECK_FALSE(client.health().stale);
    CHECK(client.isBookReady());
    CHECK(client.book().bidLevels.size() == bookBefore.bidLevels.size());
    CHECK(client.book().askLevels.size() == bookBefore.askLevels.size());
    CHECK(client.book().lastSequence() == 19);
    CHECK(client.health().sequenceGaps == 0);
    CHECK(client.health().messagesAccepted == 2);
    CHECK(source.resubscribeCalls == resubscribesBefore);
    CHECK(client.health().lastMessageAgeMs == 0);
}

TEST_CASE("real prevSeqId gap invalidates the book, counts once, and recovers via snapshot")
{
    qx::feed::test::MockFeedSource source;
    qx::feed::FeedClient client(source, testConfig(std::chrono::milliseconds(5000)));

    TestClock clock;
    client.setClock([&clock] { return clock.nowNs; });
    client.start();

    source.deliver(okxSnapshot(100));
    source.deliver(okxBidUpdate(110, 100, "100.5", "1.0"));
    REQUIRE(client.isBookReady());

    const int resubscribesBefore = source.resubscribeCalls;

    // Baseline is 110; this message claims prevSeqId=109: genuine gap.
    source.deliver(okxBidUpdate(115, 109, "99.25", "1.0"));

    CHECK(client.health().sequenceGaps == 1);
    CHECK(source.resubscribeCalls == resubscribesBefore + 1);
    CHECK(client.book().empty());
    CHECK_FALSE(client.isBookReady());

    SUBCASE("fresh snapshot re-anchors the pipeline")
    {
        source.deliver(okxSnapshot(200));
        CHECK(client.isBookReady());
        CHECK(client.book().lastSequence() == 200);

        source.deliver(okxBidUpdate(205, 200, "98.5", "1.0"));
        CHECK(client.book().lastSequence() == 205);
        CHECK(client.health().sequenceGaps == 1);
    }

    SUBCASE("post-gap deltas stay rejected until the snapshot arrives")
    {
        source.deliver(okxBidUpdate(116, 115, "97.0", "1.0"));
        CHECK_FALSE(client.isBookReady());
        CHECK(client.book().empty());
        CHECK(client.health().sequenceGaps == 1);
    }
}

TEST_CASE("OKX maintenance reset is accepted and rebases the chain lower")
{
    qx::feed::test::MockFeedSource source;
    qx::feed::FeedClient client(source, testConfig(std::chrono::milliseconds(5000)));

    TestClock clock;
    client.setClock([&clock] { return clock.nowNs; });
    client.start();

    source.deliver(okxSnapshot(25));
    REQUIRE(client.isBookReady());

    // Maintenance reset: prevSeqId connects (25), new seqId is LOWER (3).
    source.deliver(okxBidUpdate(3, 25, "100.75", "0.5"));
    CHECK(client.isBookReady());
    CHECK(client.book().lastSequence() == 3);
    CHECK(client.health().sequenceGaps == 0);

    source.deliver(okxBidUpdate(5, 3, "100.8", "0.6"));
    CHECK(client.book().lastSequence() == 5);
    CHECK(client.health().sequenceGaps == 0);
}

TEST_CASE("duplicate seqId carrying levels is stale-rejected without state damage")
{
    qx::feed::test::MockFeedSource source;
    qx::feed::FeedClient client(source, testConfig(std::chrono::milliseconds(5000)));

    TestClock clock;
    client.setClock([&clock] { return clock.nowNs; });
    client.start();

    source.deliver(okxSnapshot(110));
    REQUIRE(client.isBookReady());
    const auto before = client.book();

    source.deliver(okxBidUpdate(110, 110, "50.0", "9.0"));

    CHECK(client.health().staleRejected == 1);
    CHECK(client.book().bidLevels.size() == before.bidLevels.size());
    CHECK(client.book().lastSequence() == 110);
    CHECK(client.health().sequenceGaps == 0);
}

TEST_CASE("deltas arriving before the snapshot neither count gaps nor spam resubscribes")
{
    qx::feed::test::MockFeedSource source;
    qx::feed::FeedClient client(source, testConfig(std::chrono::milliseconds(5000)));

    TestClock clock;
    client.setClock([&clock] { return clock.nowNs; });
    client.start();

    const int resubscribesAfterStart = source.resubscribeCalls;

    source.deliver(okxBidUpdate(500, 499, "10.0", "1.0"));

    CHECK_FALSE(client.isBookReady());
    CHECK(client.health().sequenceGaps == 0);
    CHECK(client.health().messagesAccepted == 0);
    CHECK(source.resubscribeCalls == resubscribesAfterStart);

    source.deliver(okxSnapshot(501));
    CHECK(client.isBookReady());
    CHECK(client.book().lastSequence() == 501);
}

TEST_CASE("current OKX books policy ignores the deprecated checksum=0 field")
{
    qx::feed::test::MockFeedSource source;
    qx::feed::FeedClient client(source, testConfig(std::chrono::milliseconds(5000)));

    TestClock clock;
    client.setClock([&clock] { return clock.nowNs; });
    client.start();

    // Live OKX sends checksum: 0 on every books message since deprecation.
    const std::string snapWithZeroChecksum = R"({"action":"snapshot","data":[{)"
        R"("seqId":10,"prevSeqId":-1,"checksum":0,)"
        R"("bids":[["100.0","1.0"]],"asks":[["101.0","1.0"]],"ts":"1755850000000"}]})";
    source.deliver(snapWithZeroChecksum);

    CHECK(client.isBookReady());
    CHECK(client.health().checksumFailures == 0);
    CHECK(client.health().messagesAccepted == 1);
}

TEST_CASE("genuinely-enabled checksum mismatch invalidates the book and blocks simulation")
{
    qx::feed::test::MockFeedSource source;
    auto config = testConfig(std::chrono::milliseconds(5000));
    config.integrityPolicy = qx::IntegrityPolicy::SequenceAndChecksum;
    qx::feed::FeedClient client(source, config);

    TestClock clock;
    client.setClock([&clock] { return clock.nowNs; });
    client.start();

    // 1-2. Valid snapshot (no checksum field -> NotPresent -> pass), book ready.
    source.deliver(okxSnapshot(10));
    REQUIRE(client.isBookReady());

    const int resubscribesBefore = source.resubscribeCalls;

    // 3-4. Accepted update carrying an intentionally wrong checksum.
    const std::string badChecksumUpdate = R"({"action":"update","data":[{)"
        R"("seqId":15,"prevSeqId":10,"checksum":123456789,)"
        R"("bids":[["99.5","1.0"]],"asks":[],"ts":"1755850001000"}]})";
    source.deliver(badChecksumUpdate);

    CHECK(client.health().checksumFailures == 1);
    // 5-6. Book unavailable; simulate cannot pretend the corrupt book is healthy.
    CHECK_FALSE(client.isBookReady());
    CHECK(client.book().empty());
    const auto blocked = client.simulate(
        { qx::Side::Buy, qx::SizeMode::Notional, 25000.0, 5.0 });
    CHECK(blocked.insufficientLiquidity);
    // 7. Resubscribe requested for a fresh snapshot.
    CHECK(source.resubscribeCalls == resubscribesBefore + 1);

    // 8. Fresh valid snapshot restores availability and simulations work.
    source.deliver(okxSnapshot(20));
    REQUIRE(client.isBookReady());
    const auto result = client.simulate(
        { qx::Side::Buy, qx::SizeMode::Notional, 50.0, 5.0 });
    CHECK_FALSE(result.insufficientLiquidity);
    CHECK(result.filledBaseQty > 0.0);
}

TEST_CASE("malformed frame storm is survivable and counted")
{
    qx::feed::test::MockFeedSource source;
    qx::feed::FeedClient client(source, testConfig(std::chrono::milliseconds(5000)));

    TestClock clock;
    client.setClock([&clock] { return clock.nowNs; });
    client.start();

    for (int i = 0; i < 50; ++i) {
        source.deliver("garbage{{{not json");
        source.deliver(R"({"action":"update","data":[]})");
        source.deliver(R"({"action":"update","data":[{"seqId":1,"bids":[["x","y"]],"asks":[]}]})");
    }

    CHECK(client.health().malformedMessages == 150);
    CHECK_FALSE(client.isBookReady());

    // Stream continues normally afterwards.
    source.deliver(okxSnapshot(100));
    source.deliver(okxBidUpdate(120, 100, "100.5", "1.0"));

    CHECK(client.isBookReady());
    CHECK(client.book().lastSequence() == 120);
    CHECK(client.health().malformedMessages == 150);
    CHECK(client.health().messagesAccepted == 2);
}

TEST_CASE("arrival mode keeps accepting unsequenced proxy frames")
{
    qx::feed::test::MockFeedSource source;
    qx::feed::FeedClient client(source, testConfig(std::chrono::milliseconds(5000)));
    // Arrival-mode config is exercised through the validator directly below;
    // the default FeedClient config remains strict-sequenced by design.

    TestClock clock;
    client.setClock([&clock] { return clock.nowNs; });

    qx::SequenceValidator validator(qx::SequenceValidator::Mode::Arrival);

    const std::string proxySnap = R"({
        "asks": [["101.0", "4.0"]],
        "bids": [["100.0", "5.0"]],
        "timestamp_ns": 1755850000000000000
    })";

    REQUIRE(validator.validate(qx::decodeFrame(proxySnap, qx::FeedFormat::ProxyBooks).event)
            .verdict == qx::SequenceValidator::Verdict::Accept);
    REQUIRE(validator.validate(qx::decodeFrame(proxySnap, qx::FeedFormat::ProxyBooks).event)
            .verdict == qx::SequenceValidator::Verdict::Accept);

    CHECK(validator.stats().accepted == 2);
    CHECK(client.book().empty()); // nothing delivered to the client itself
}

TEST_CASE("concurrent readers see consistent state while a sequenced stream flows")
{
    qx::feed::test::MockFeedSource source;
    qx::feed::FeedClient client(source, testConfig(std::chrono::milliseconds(60000)));

    TestClock clock;
    client.setClock([&clock] { return clock.nowNs; });

    client.start();
    source.deliver(okxSnapshot(1));

    std::atomic<bool> done { false };

    std::vector<std::thread> readers;
    for (int r = 0; r < 4; ++r) {
        readers.emplace_back([&client, &done] {
            while (!done.load()) {
                (void)client.isBookReady();
                (void)client.health();
                (void)client.book().bids().size();
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            }
        });
    }

    // Chained updates: each connects to the previously accepted seqId, with
    // forward jumps mixed in.
    std::uint64_t seq = 1;
    for (int i = 0; i < 400; ++i) {
        const std::uint64_t next = seq + 1 + (i % 3);
        source.deliver(okxBidUpdate(next, static_cast<std::int64_t>(seq), "99.5", "1.0"));
        seq = next;
    }

    done.store(true);
    for (auto& thread : readers) {
        thread.join();
    }

    CHECK(client.book().lastSequence() == seq);
    CHECK(client.health().sequenceGaps == 0);
}