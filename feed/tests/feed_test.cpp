#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "MockFeedSource.h"
#include "qx/feed/FeedClient.h"

namespace {

std::string okxSnapshot(std::uint64_t seq)
{
    return R"({"action":"snapshot","data":[{"seq":)" + std::to_string(seq)
        + R"(,"bids":[["100.0","5.0"],["99.0","3.0"]],"asks":[["101.0","4.0"]]}]})";
}

std::string okxDelta(std::uint64_t seq, const std::string& price)
{
    return R"({"action":"update","data":[{"seq":)" + std::to_string(seq)
        + R"(,"bids":[[")" + price + R"(","1.0"]],"asks":[]}]})";
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

TEST_CASE("sequence gap drops the book, counts one gap, and resyncs")
{
    qx::feed::test::MockFeedSource source;
    qx::feed::FeedClient client(source, testConfig(std::chrono::milliseconds(5000)));

    TestClock clock;
    client.setClock([&clock] { return clock.nowNs; });

    client.start();

    source.deliver(okxSnapshot(100));
    source.deliver(okxDelta(101, "99.5"));
    REQUIRE(client.isBookReady());
    CHECK(client.book().lastSequence() == 101);
    const int resubscribesBefore = source.resubscribeCalls;

    source.deliver(okxDelta(103, "99.25"));

    CHECK(client.health().sequenceGaps == 1);
    CHECK(source.resubscribeCalls == resubscribesBefore + 1);
    CHECK(client.book().empty());

    SUBCASE("fresh snapshot restores the pipeline")
    {
        source.deliver(okxSnapshot(200));
        CHECK(client.isBookReady());
        CHECK(client.book().lastSequence() == 200);

        source.deliver(okxDelta(201, "98.5"));
        CHECK(client.book().lastSequence() == 201);
        CHECK(client.health().sequenceGaps == 1);
    }
}

TEST_CASE("staleness is flagged past the threshold and cleared by data")
{
    qx::feed::test::MockFeedSource source;
    qx::feed::FeedClient client(source, testConfig(std::chrono::milliseconds(1000)));

    TestClock clock;
    client.setClock([&clock] { return clock.nowNs; });

    client.start();
    source.deliver(okxSnapshot(100));

    CHECK_FALSE(client.health().stale);

    clock.nowNs += 900'000'000;
    client.tick();
    CHECK_FALSE(client.health().stale);

    clock.nowNs += 200'000'000;
    client.tick();
    CHECK(client.health().stale);

    source.deliver(okxDelta(101, "99.5"));
    CHECK_FALSE(client.health().stale);
}

TEST_CASE("malformed frames are counted and the stream continues")
{
    qx::feed::test::MockFeedSource source;
    qx::feed::FeedClient client(source, testConfig(std::chrono::milliseconds(5000)));

    TestClock clock;
    client.setClock([&clock] { return clock.nowNs; });

    client.start();

    source.deliver("not json {{{");
    source.deliver(R"({"action":"update","data":[{"seq":5,"bids":[[1]],"asks":[]}]})");

    CHECK(client.health().malformedMessages == 2);
    CHECK_FALSE(client.isBookReady());

    source.deliver(okxSnapshot(100));
    CHECK(client.isBookReady());
    CHECK(client.health().messagesAccepted == 1);
    CHECK(client.health().malformedMessages == 2);
}

TEST_CASE("concurrent readers see consistent state while updates stream")
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

    for (std::uint64_t seq = 2; seq < 400; ++seq) {
        source.deliver(okxDelta(seq, "99.5"));
    }

    done.store(true);
    for (auto& thread : readers) {
        thread.join();
    }

    CHECK(client.book().lastSequence() == 399);
}
