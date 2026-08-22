#include "qx/feed/FeedClient.h"

namespace qx::feed {

namespace {

std::int64_t steadyClockNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

FeedClient::FeedClient(FeedSource& source, FeedConfig config)
    : source_(source)
    , config_(config)
    , validator_(config.validatorMode)
{
    clockNs_ = &steadyClockNs;

    source_.onFrame([this](const std::string_view payload) { handleFrame(payload); });
    source_.onStateChange([this](const ConnectionState state) { handleState(state); });
}

void FeedClient::setClock(std::function<std::int64_t()> clockNs)
{
    const std::lock_guard lock(mutex_);
    clockNs_ = std::move(clockNs);
}

void FeedClient::start()
{
    {
        const std::lock_guard lock(mutex_);
        phase_ = Phase::Connecting;
        health_.state = ConnectionState::Connecting;
    }
    source_.connect();
}

void FeedClient::stop()
{
    {
        const std::lock_guard lock(mutex_);
        phase_ = Phase::Idle;
        health_.state = ConnectionState::Disconnected;
    }
    source_.disconnect();
}

void FeedClient::handleState(const ConnectionState state)
{
    bool resubscribeRequested = false;

    {
        const std::lock_guard lock(mutex_);

        const auto now = clockNs_();
        health_.state = state;

        switch (state) {
        case ConnectionState::Connected:
            phase_ = Phase::Connected;
            if (everConnected_) {
                ++health_.reconnects;
            }
            everConnected_ = true;
            reconnectAttempt_ = 0;
            resetPipelineLocked();
            resubscribeRequested = true;
            break;

        case ConnectionState::Connecting:
            phase_ = Phase::Connecting;
            break;

        case ConnectionState::Disconnected:
            if (phase_ == Phase::Connected || phase_ == Phase::Connecting) {
                pendingReconnectDelay_ = config_.reconnectPolicy.delayForAttempt(reconnectAttempt_);
                reconnectDueAtNs_ = now
                    + std::chrono::duration_cast<std::chrono::nanoseconds>(pendingReconnectDelay_)
                          .count();
                ++reconnectAttempt_;
                phase_ = Phase::WaitingReconnect;
            }
            break;
        }
    }

    if (resubscribeRequested) {
        source_.resubscribe();
    }
}

void FeedClient::tick()
{
    bool fireReconnect = false;
    {
        const std::lock_guard lock(mutex_);

        const auto now = clockNs_();

        switch (phase_) {
        case Phase::WaitingReconnect:
            if (now >= reconnectDueAtNs_) {
                phase_ = Phase::Connecting;
                health_.state = ConnectionState::Connecting;
                fireReconnect = true;
            }
            break;

        case Phase::Connected: {
            const auto stalenessNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                config_.stalenessThreshold)
                .count();
            if (lastMessageNs_ != 0 && now - lastMessageNs_ > stalenessNs && !health_.stale) {
                health_.stale = true;
            }
            break;
        }

        case Phase::Idle:
        case Phase::Connecting:
            break;
        }
    }

    if (fireReconnect) {
        source_.connect();
    }
}

void FeedClient::resetPipelineLocked()
{
    book_.clear();
    validator_.reset();
}

void FeedClient::handleFrame(const std::string_view payload)
{
    bool resubscribeRequested = false;

    {
        const std::lock_guard lock(mutex_);

        const auto now = clockNs_();

        const auto decoded = qx::decodeFrame(payload, config_.format);
        if (decoded.status != qx::DecodeStatus::Ok) {
            ++health_.malformedMessages;
            return;
        }

        MarketEvent event = decoded.event;

        const auto verdict = validator_.validate(event);

        switch (verdict.verdict) {
        case SequenceValidator::Verdict::Accept:
            break;

        case SequenceValidator::Verdict::StaleReject:
            ++health_.staleRejected;
            return;

        case SequenceValidator::Verdict::GapResync:
            ++health_.sequenceGaps;
            resetPipelineLocked();
            resubscribeRequested = true;
            break;
        }

        if (!resubscribeRequested) {
            event.sequence = verdict.effectiveSequence;

            if (event.type == EventType::Snapshot) {
                book_.applySnapshot(event);
            } else {
                (void)book_.applyDelta(event);
            }

            if (!validator_.verifyChecksum(book_, event)) {
                ++health_.checksumFailures;
            }

            lastMessageNs_ = now;
            health_.stale = false;
            health_.bookReady = true;
            ++health_.messagesAccepted;
        }
    }

    if (resubscribeRequested) {
        source_.resubscribe();
    }
}

FeedClient::BookView FeedClient::book() const
{
    const std::lock_guard lock(mutex_);

    BookView view;
    view.bidLevels = book_.bids();
    view.askLevels = book_.asks();
    view.sequence = book_.lastSequence();
    return view;
}

bool FeedClient::isBookReady() const
{
    const std::lock_guard lock(mutex_);
    return health_.bookReady;
}

ExecutionResult FeedClient::simulate(const OrderRequest& request) const
{
    const std::lock_guard lock(mutex_);
    const ExecutionSimulator simulator;
    return simulator.execute(book_, request);
}

FeedHealth FeedClient::health() const
{
    const std::lock_guard lock(mutex_);

    FeedHealth out = health_;
    out.state = health_.state;

    if (phase_ == Phase::Connected && lastMessageNs_ != 0) {
        out.lastMessageAgeMs = (clockNs_() - lastMessageNs_) / 1'000'000;
    }

    return out;
}

} // namespace qx::feed
