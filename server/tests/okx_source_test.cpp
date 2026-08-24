// Deterministic transport-state coverage for OkxWebSocketSource.
//
// Drives the REAL IXWebSocket machinery (Open / Message / Close / Error
// callbacks plus the liveness worker) against an in-process ix::WebSocketServer
// bound to loopback. No external network, no OKX dependency: CI stays
// hermetic while the production transport code path is exercised end-to-end.
//
// Covered here:
//   - Connecting -> Connected -> Disconnected transitions are emitted in order
//   - connected_/session state transitions are race-free (TSan scans these)
//   - exactly one Disconnected per remote close, no duplicates/disorder
//   - service-upgrade notice teardown never deadlocks or self-joins
//   - pong-timeout supervision fires and counts missed pongs
//   - stop()/disconnect() joins cleanly: zero callbacks afterwards

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXWebSocketServer.h>

#include "qx/feed/FeedSource.h"
#include "qx/feed/OkxWebSocketSource.h"

namespace {

using namespace std::chrono_literals;

// Real-shape OKX v5 books payloads (mirrors feed_test.cpp fixtures).
std::string okxSnapshot(std::uint64_t seqId)
{
    return R"({"arg":{"channel":"books","instId":"BTC-USDT"},"action":"snapshot",)"
        R"("data":[{"seqId":)" + std::to_string(seqId) + R"(,"prevSeqId":-1,)"
        R"("bids":[["100.0","5.0"]],"asks":[["101.0","4.0"]],)"
        R"("ts":"1755850000000"}]})";
}

std::string okxBidUpdate(std::uint64_t seqId, std::int64_t prevSeqId)
{
    return R"({"action":"update","data":[{"seqId":)" + std::to_string(seqId)
        + R"(,"prevSeqId":)" + std::to_string(prevSeqId)
        + R"(,"bids":[["99.5","2.0"]],"asks":[],"ts":"1755850001000"}]})";
}

template <typename Predicate>
bool waitFor(Predicate&& predicate, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return predicate();
}

int nextCandidatePort()
{
    static std::atomic<int> next {
        20000 + static_cast<int>(
                    std::chrono::steady_clock::now().time_since_epoch().count()
                    % 20000)
    };
    return next.fetch_add(13);
}

// Loopback stand-in for the OKX endpoint: accepts one client, records what
// arrives, answers application-level pings, and lets the test push payloads
// or close the connection remotely.
class StubOkxServer {
public:
    bool start()
    {
        for (int attempt = 0; attempt < 32; ++attempt) {
            auto server = std::make_unique<ix::WebSocketServer>(
                nextCandidatePort(), "127.0.0.1");

            server->setOnConnectionCallback(
                [this](std::weak_ptr<ix::WebSocket> clientWeak,
                    std::shared_ptr<ix::ConnectionState>) {
                    // IXWebSocket contract: when an OnConnectionCallback is
                    // installed, the per-client message callback must be
                    // registered here.
                    const auto client = clientWeak.lock();
                    if (!client) {
                        return;
                    }
                    {
                        const std::lock_guard lock(mutex_);
                        client_ = client;
                    }
                    client->setOnMessageCallback(
                        [this, client](const ix::WebSocketMessagePtr& message) {
                            if (message->type != ix::WebSocketMessageType::Message) {
                                return;
                            }
                            bool answerPing = false;
                            {
                                const std::lock_guard lock(mutex_);
                                ++received_;
                                lastPayload_ = message->str;
                                answerPing = answerPings_;
                            }
                            if (answerPing && message->str == "ping") {
                                client->send("pong");
                            }
                        });
                });

            if (server->listen().first) {
                port_ = server->getPort();
                server->start();
                server_ = std::move(server);
                return true;
            }
        }
        return false;
    }

    void stop()
    {
        if (server_) {
            server_->stop();
            server_.reset();
        }
    }

    int port() const { return port_; }

    std::size_t receivedCount() const
    {
        const std::lock_guard lock(mutex_);
        return received_;
    }

    std::string lastPayload() const
    {
        const std::lock_guard lock(mutex_);
        return lastPayload_;
    }

    void setAnswerPings(bool enabled)
    {
        const std::lock_guard lock(mutex_);
        answerPings_ = enabled;
    }

    void sendToClient(const std::string& payload)
    {
        std::shared_ptr<ix::WebSocket> client;
        {
            const std::lock_guard lock(mutex_);
            client = client_.lock();
        }
        REQUIRE(client != nullptr);
        client->send(payload);
    }

    bool closeClient(std::uint16_t code, const std::string& reason)
    {
        std::shared_ptr<ix::WebSocket> client;
        {
            const std::lock_guard lock(mutex_);
            client = client_.lock();
        }
        if (!client) {
            return false;
        }
        client->close(code, reason);
        return true;
    }

private:
    mutable std::mutex mutex_;
    std::unique_ptr<ix::WebSocketServer> server_;
    std::weak_ptr<ix::WebSocket> client_;
    std::size_t received_ = 0;
    std::string lastPayload_;
    bool answerPings_ = true;
    int port_ = 0;
};

class StateLog {
public:
    void push(qx::feed::ConnectionState state)
    {
        const std::lock_guard lock(mutex_);
        states_.push_back(state);
    }

    std::size_t countOf(qx::feed::ConnectionState state) const
    {
        const std::lock_guard lock(mutex_);
        return static_cast<std::size_t>(
            std::count(states_.begin(), states_.end(), state));
    }

    std::size_t size() const
    {
        const std::lock_guard lock(mutex_);
        return states_.size();
    }

    std::vector<qx::feed::ConnectionState> snapshot() const
    {
        const std::lock_guard lock(mutex_);
        return states_;
    }

private:
    mutable std::mutex mutex_;
    std::vector<qx::feed::ConnectionState> states_;
};

class FrameLog {
public:
    void push(std::string_view payload)
    {
        const std::lock_guard lock(mutex_);
        payloads_.emplace_back(payload);
    }

    std::vector<std::string> snapshot() const
    {
        const std::lock_guard lock(mutex_);
        return payloads_;
    }

private:
    mutable std::mutex mutex_;
    std::vector<std::string> payloads_;
};

struct Harness {
    StubOkxServer server;
    StateLog states;
    FrameLog frames;
    std::unique_ptr<qx::feed::OkxWebSocketSource> source;

    void attach(const qx::feed::OkxWebSocketSource::Config& config)
    {
        source = std::make_unique<qx::feed::OkxWebSocketSource>(config);
        source->onStateChange([this](const qx::feed::ConnectionState state) {
            states.push(state);
        });
        source->onFrame([this](const std::string_view payload) {
            frames.push(payload);
        });
    }
};

std::string expectedSubscribePayload(const std::string& channel,
    const std::string& instrument)
{
    return "{\"op\":\"subscribe\",\"args\":[{\"channel\":\"" + channel
        + "\",\"instId\":\"" + instrument + "\"}]}";
}

void requireOrderedPrefix(const std::vector<qx::feed::ConnectionState>& states,
    const std::vector<qx::feed::ConnectionState>& prefix)
{
    REQUIRE(states.size() >= prefix.size());
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        INFO("state[", i, "] mismatch");
        CHECK(states[i] == prefix[i]);
    }
}

} // namespace

TEST_CASE("transport transitions Connecting->Connected->Disconnected and reconnects cleanly")
{
    Harness h;
    REQUIRE(h.server.start());

    qx::feed::OkxWebSocketSource::Config config;
    config.url = "ws://127.0.0.1:" + std::to_string(h.server.port());
    config.pingInterval = 60s;
    config.pongTimeout = 30s;
    config.livenessTick = 20ms;
    h.attach(config);

    h.source->connect();

    // Handshake completes through the genuine IXWebSocket Open callback.
    REQUIRE(waitFor([&] {
        return h.states.countOf(qx::feed::ConnectionState::Connected) >= 1;
    }, 10s));
    CHECK(h.source->health().upgradeNotices == 0);

    REQUIRE(waitFor([&] { return h.server.receivedCount() >= 1; }, 5s));
    CHECK(h.server.lastPayload()
        == expectedSubscribePayload("books", "BTC-USDT"));

    // Snapshot and one incremental flow upward, in order.
    h.server.sendToClient(okxSnapshot(100));
    REQUIRE(waitFor([&] { return h.frames.snapshot().size() == 1; }, 5s));
    h.server.sendToClient(okxBidUpdate(105, 100));
    REQUIRE(waitFor([&] { return h.frames.snapshot().size() == 2; }, 5s));
    CHECK(h.frames.snapshot()[0] == okxSnapshot(100));
    CHECK(h.frames.snapshot()[1] == okxBidUpdate(105, 100));

    // Remote close: exactly ONE synchronized Disconnected, session retires.
    REQUIRE(h.server.closeClient(1000, "stub maintenance"));
    REQUIRE(waitFor([&] {
        return h.states.countOf(qx::feed::ConnectionState::Disconnected) == 1;
    }, 10s));

    // FeedClient-style backoff expiry: connect() must reap the retired worker
    // and establish a fresh PHYSICAL session (new subscribe over the wire).
    REQUIRE(waitFor([&] {
        h.source->connect();
        return h.states.countOf(qx::feed::ConnectionState::Connected) >= 2;
    }, 10s));

    REQUIRE(waitFor([&] { return h.server.receivedCount() >= 2; }, 5s));
    h.server.sendToClient(okxSnapshot(200));
    REQUIRE(waitFor([&] { return h.frames.snapshot().size() == 3; }, 5s));

    // No duplicate/disordered transitions across both sessions.
    const auto ordered = h.states.snapshot();
    requireOrderedPrefix(ordered, {
        qx::feed::ConnectionState::Connecting,
        qx::feed::ConnectionState::Connected,
        qx::feed::ConnectionState::Disconnected,
        qx::feed::ConnectionState::Connecting,
        qx::feed::ConnectionState::Connected,
    });
    CHECK(h.states.countOf(qx::feed::ConnectionState::Connecting) == 2);

    // Clean stop: disconnect() returns only after the worker joined, so no
    // further state can appear afterwards.
    h.source->disconnect();
    const auto settledSize = h.states.size();
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    CHECK(h.states.size() == settledSize);

    h.server.stop();
}

TEST_CASE("service-upgrade notice tears down cleanly without deadlock")
{
    Harness h;
    REQUIRE(h.server.start());

    qx::feed::OkxWebSocketSource::Config config;
    config.url = "ws://127.0.0.1:" + std::to_string(h.server.port());
    config.pingInterval = 60s;
    config.pongTimeout = 30s;
    config.livenessTick = 20ms;
    h.attach(config);

    h.source->connect();
    REQUIRE(waitFor([&] {
        return h.states.countOf(qx::feed::ConnectionState::Connected) >= 1;
    }, 10s));

    // OKX pre-maintenance notice. The handler must NOT stop the socket from
    // the callback thread (IXWebSocket::stop() self-join would terminate the
    // process) - the liveness worker performs the teardown instead.
    h.server.sendToClient(R"({"event":"notice","code":"64008","msg":"upgrade"})");

    REQUIRE(waitFor([&] {
        return h.states.countOf(qx::feed::ConnectionState::Disconnected) == 1;
    }, 10s));
    CHECK(h.source->health().upgradeNotices == 1);
    CHECK(h.frames.snapshot().empty());

    h.source->disconnect();
    h.server.stop();
}

TEST_CASE("silent peer trips the pong deadline and counts missed pongs")
{
    Harness h;
    REQUIRE(h.server.start());

    qx::feed::OkxWebSocketSource::Config config;
    config.url = "ws://127.0.0.1:" + std::to_string(h.server.port());
    config.pingInterval = 150ms;
    config.pongTimeout = 120ms;
    config.livenessTick = 25ms;
    h.attach(config);

    h.server.setAnswerPings(false);

    h.source->connect();
    REQUIRE(waitFor([&] {
        return h.states.countOf(qx::feed::ConnectionState::Connected) >= 1;
    }, 10s));

    // First ping goes unanswered, deadline elapses: supervisor stops the
    // socket from the WORKER thread and reports Disconnected.
    REQUIRE(waitFor([&] {
        return h.states.countOf(qx::feed::ConnectionState::Disconnected) == 1;
    }, 10s));
    CHECK(h.source->health().missedPongs >= 1);

    h.source->disconnect();
    h.server.stop();
}

TEST_CASE("remote close racing local disconnect stays consistent")
{
    Harness h;
    REQUIRE(h.server.start());

    qx::feed::OkxWebSocketSource::Config config;
    config.url = "ws://127.0.0.1:" + std::to_string(h.server.port());
    config.pingInterval = 60s;
    config.pongTimeout = 30s;
    config.livenessTick = 20ms;
    h.attach(config);

    h.source->connect();
    REQUIRE(waitFor([&] {
        return h.states.countOf(qx::feed::ConnectionState::Connected) >= 1;
    }, 10s));

    // Remote close lands while disconnect() is joining the worker: whichever
    // wins, shutdown must stay deadlock-free and state must remain sane.
    std::thread remoteCloser([&] { h.server.closeClient(1000, "race"); });
    h.source->disconnect();
    remoteCloser.join();

    const auto ordered = h.states.snapshot();
    REQUIRE_FALSE(ordered.empty());
    CHECK(ordered.front() == qx::feed::ConnectionState::Connecting);
    CHECK(ordered.back() == qx::feed::ConnectionState::Disconnected);
    CHECK(h.states.countOf(qx::feed::ConnectionState::Connected) == 1);

    h.server.stop();
}

int main(int argc, char** argv)
{
    ix::initNetSystem();
    const int result = doctest::Context(argc, argv).run();
    ix::uninitNetSystem();
    return result;
}
