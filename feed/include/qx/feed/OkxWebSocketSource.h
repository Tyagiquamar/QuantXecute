#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "qx/feed/FeedSource.h"

namespace qx::feed {

// Production market-data source for the public OKX v5 WebSocket API.
//
// Responsibilities (deliberately narrow):
//   - physical TLS connect/disconnect against a configurable endpoint
//   - application-level OKX liveness ("ping" / expect "pong")
//   - sending the books subscription (initial and on resubscribe())
//   - forwarding order-book frames upward, filtering OKX control events
//
// Reconnect/backoff/staleness policy stays in FeedClient: this source never
// re-connects on its own, it only reports ConnectionState transitions.
//
// Physical connection lifecycle: one worker session spans connect() until the
// transport reaches its terminal event (Close/Error) or disconnect() is
// requested. After a terminal event the worker retires itself, so a later
// connect() (FeedClient backoff expiry) establishes a fresh physical
// connection instead of being swallowed by the idempotent guard.
class OkxWebSocketSource final : public FeedSource {
public:
    struct Config {
        // Default global public endpoint; regional domains exist and OKX
        // rotates them for maintenance, hence configurable.
        std::string url = "wss://ws.okx.com:8443/ws/v5/public";

        std::string channel = "books";
        std::string instrument = "BTC-USDT";

        // Application-level liveness. OKX drops connections silent for <30s;
        // we ping well below that and allow a bounded pong window.
        // Copy-initialization form: braced duration NSDMIs trip older GCC
        // aggregate/default-argument handling, copy-init is universally safe.
        std::chrono::milliseconds pingInterval = std::chrono::milliseconds(20000);
        std::chrono::milliseconds pongTimeout = std::chrono::milliseconds(10000);
        std::chrono::milliseconds livenessTick = std::chrono::milliseconds(500);

        // Optional explicit CA bundle for TLS verification. Empty means the
        // TLS backend default (system store on Linux).
        std::string caCertPath;
    };

    // Source-side counters that do not belong to FeedHealth sequencing.
    struct SourceHealth {
        std::uint64_t upgradeNotices = 0;
        std::uint64_t errorEvents = 0;
        std::uint64_t missedPongs = 0;
    };

    // No default argument here: GCC 12/15 reject any default-argument
    // conversion for a parameter of nested-class type ("could not convert
    // brace-enclosed initializer list"). Both callers build a Config anyway.
    explicit OkxWebSocketSource(Config config);
    ~OkxWebSocketSource() override;

    OkxWebSocketSource(const OkxWebSocketSource&) = delete;
    OkxWebSocketSource& operator=(const OkxWebSocketSource&) = delete;

    void connect() override;
    void disconnect() override;
    void resubscribe() override;

    SourceHealth health() const;

private:
    void runWorker();
    void sendText(const std::string& payload);
    void handleControlEvent(const std::string& payload);
    bool looksLikeControlEvent(const std::string& payload) const;

    Config config_;

    // -------------------------------------------------------------------------
    // Synchronization model (audited):
    //
    //   mutex_-owned (every access holds mutex_; no exceptions):
    //     handle_           physical IXWebSocket instance (null off-session)
    //     workerRunning_    worker thread exists / session slot occupied
    //     stopRequested_    disconnect() requested
    //     connected_        transport-level Open seen, no terminal event yet
    //     sessionTerminal_  transport delivered Close/Error for this session
    //     closeRequested_   internal stop requested (upgrade notice)
    //     pingOutstanding_  application-level ping awaiting "pong"
    //
    //   atomic (lock-free simple access only):
    //     lastActivityNs_   written from IXWebSocket callback threads
    //     upgradeNotices_ / errorEvents_ / missedPongs_
    //
    // Threading contract: IXWebSocket invokes the message callback on its own
    // background thread; the liveness loop runs on worker_; resubscribe() is
    // driven by the owning FeedClient host thread. Callbacks mutate
    // mutex_-owned state only - they never block on the socket while holding
    // mutex_ and never call emitState()/emitFrame() under the lock (handlers
    // may re-enter resubscribe()). connect()/disconnect() additionally hold
    // lifecycleMutex_ so reaping/spawning the worker thread is atomic even if
    // a future caller violates the single-host-thread convention.
    // -------------------------------------------------------------------------

    mutable std::mutex mutex_;
    std::mutex lifecycleMutex_;
    std::condition_variable cv_;
    std::unique_ptr<class IxWebSocketHandle> handle_;
    std::thread worker_;
    bool workerRunning_ = false;
    bool stopRequested_ = false;
    bool connected_ = false;
    bool sessionTerminal_ = false;
    bool closeRequested_ = false;
    bool pingOutstanding_ = false;

    std::atomic<std::int64_t> lastActivityNs_ { 0 };

    std::atomic<std::uint64_t> upgradeNotices_ { 0 };
    std::atomic<std::uint64_t> errorEvents_ { 0 };
    std::atomic<std::uint64_t> missedPongs_ { 0 };
};

} // namespace qx::feed