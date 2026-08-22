#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

#include "qx/Book.h"
#include "qx/Decoder.h"
#include "qx/ExecutionSimulator.h"
#include "qx/SequenceValidator.h"

#include "qx/feed/FeedSource.h"
#include "qx/feed/ReconnectPolicy.h"

namespace qx::feed {

struct FeedConfig {
    qx::FeedFormat format = qx::FeedFormat::OkxBooks;

    // Continuity semantics owned by SequenceValidator.
    qx::SequenceValidator::Mode validatorMode = qx::SequenceValidator::Mode::OkxSequenced;

    // Which integrity guarantees this feed actually provides. Current OKX
    // books channels use SequenceOnly: their deprecated checksum field is
    // fixed to 0 and must never be verified.
    qx::IntegrityPolicy integrityPolicy = qx::IntegrityPolicy::SequenceOnly;

    std::chrono::milliseconds stalenessThreshold { 5000 };
    ReconnectPolicy reconnectPolicy = ReconnectPolicy::defaults();
};

struct FeedHealth {
    ConnectionState state = ConnectionState::Disconnected;

    std::uint64_t reconnects = 0;
    std::uint64_t sequenceGaps = 0;
    std::uint64_t malformedMessages = 0;
    std::uint64_t staleRejected = 0;
    std::uint64_t checksumFailures = 0;
    std::uint64_t messagesAccepted = 0;

    bool stale = false;
    bool bookReady = false;
    std::int64_t lastMessageAgeMs = -1;
};

class FeedClient {
public:
    FeedClient(FeedSource& source, FeedConfig config = {});

    struct BookView {
        std::vector<Level> bidLevels;
        std::vector<Level> askLevels;
        std::uint64_t sequence = 0;

        std::vector<Level> bids() const { return bidLevels; }
        std::vector<Level> asks() const { return askLevels; }
        std::uint64_t lastSequence() const { return sequence; }
        bool empty() const { return bidLevels.empty() && askLevels.empty(); }
    };

    void setClock(std::function<std::int64_t()> clockNs);

    void start();
    void stop();
    void tick();

    BookView book() const;
    bool isBookReady() const;

    // Simulates against the current accepted book. When the pipeline is not
    // ready (no snapshot yet, or invalidated by a gap/checksum failure) the
    // result reports insufficientLiquidity instead of pretending health.
    ExecutionResult simulate(const OrderRequest& request) const;

    FeedHealth health() const;

private:
    enum class Phase : std::uint8_t {
        Idle,
        WaitingReconnect,
        Connecting,
        Connected,
    };

    void handleFrame(std::string_view payload);
    void handleState(ConnectionState state);
    void resetPipelineLocked();
    void invalidateBookLocked();

    FeedSource& source_;
    FeedConfig config_;

    mutable std::mutex mutex_;
    Book book_;
    SequenceValidator validator_;
    FeedHealth health_;

    Phase phase_ = Phase::Idle;
    bool everConnected_ = false;
    std::chrono::milliseconds pendingReconnectDelay_ { 0 };
    std::uint32_t reconnectAttempt_ = 0;
    std::int64_t reconnectDueAtNs_ = 0;

    std::int64_t lastMessageNs_ = 0;
    std::function<std::int64_t()> clockNs_;
};

} // namespace qx::feed