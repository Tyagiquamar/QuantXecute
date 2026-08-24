#include "qx/feed/OkxWebSocketSource.h"

#include <chrono>
#include <utility>

#include <ixwebsocket/IXWebSocket.h>

namespace qx::feed {

// Thin wrapper so the ixwebsocket header stays out of our public header.
// Lives at qx::feed namespace scope to match the forward declaration in
// OkxWebSocketSource.h - an anonymous-namespace definition here would be a
// DIFFERENT type and leave the header's handle_ permanently incomplete.
class IxWebSocketHandle {
public:
    ix::WebSocket socket;
};

namespace {

std::int64_t steadyNowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

OkxWebSocketSource::OkxWebSocketSource(Config config)
    : config_(std::move(config))
{
}

OkxWebSocketSource::~OkxWebSocketSource()
{
    disconnect();
}

void OkxWebSocketSource::connect()
{
    // Serializes reaping/spawning the worker against disconnect() and any
    // concurrent connect(); mutex_ below guards the state itself.
    const std::lock_guard lifecycleLock(lifecycleMutex_);
    {
        const std::lock_guard lock(mutex_);
        if (workerRunning_ && !sessionTerminal_) {
            // Physical connect is idempotent while a session is being
            // established or is live.
            return;
        }
        if (workerRunning_) {
            // Previous session already saw its terminal event; wake the
            // retiring worker so teardown completes here instead of racing
            // ahead of the fresh spawn below.
            cv_.notify_all();
        }
    }

    // Reap the retired worker. Safe: it exits its loop once sessionTerminal_
    // or stopRequested_ holds, and it never blocks while holding mutex_.
    if (worker_.joinable()) {
        worker_.join();
    }

    {
        const std::lock_guard lock(mutex_);
        workerRunning_ = true;
        stopRequested_ = false;
        connected_ = false;
        sessionTerminal_ = false;
        closeRequested_ = false;
        pingOutstanding_ = false;
    }

    emitState(ConnectionState::Connecting);
    worker_ = std::thread([this] { runWorker(); });
}

void OkxWebSocketSource::disconnect()
{
    const std::lock_guard lifecycleLock(lifecycleMutex_);
    {
        std::lock_guard lock(mutex_);
        stopRequested_ = true;
    }
    cv_.notify_all();

    if (worker_.joinable()) {
        worker_.join();
    }
}

void OkxWebSocketSource::resubscribe()
{
    std::string subscribeMessage;
    {
        const std::lock_guard lock(mutex_);
        if (!connected_ || !handle_) {
            return;
        }
        subscribeMessage = "{\"op\":\"subscribe\",\"args\":[{\"channel\":\""
            + config_.channel + "\",\"instId\":\"" + config_.instrument + "\"}]}";
    }
    sendText(subscribeMessage);
}

OkxWebSocketSource::SourceHealth OkxWebSocketSource::health() const
{
    SourceHealth out;
    out.upgradeNotices = upgradeNotices_;
    out.errorEvents = errorEvents_;
    out.missedPongs = missedPongs_;
    return out;
}

void OkxWebSocketSource::sendText(const std::string& payload)
{
    const std::lock_guard lock(mutex_);
    if (handle_) {
        const auto result = handle_->socket.send(payload);
        if (result.success) {
            return;
        }
    }
    ++errorEvents_;
}

bool OkxWebSocketSource::looksLikeControlEvent(const std::string& payload) const
{
    // OKX control events (subscribe ack / error / notice) carry an "event"
    // member before any "arg"; order-book frames carry "arg" only.
    const auto eventPos = payload.find("\"event\"");
    if (eventPos == std::string::npos) {
        return false;
    }
    const auto argPos = payload.find("\"arg\"");
    return argPos == std::string::npos || eventPos < argPos;
}
void OkxWebSocketSource::handleControlEvent(const std::string& payload)
{
    //   {"event":"subscribe",...}          -> subscription ack (informational)
    //   {"event":"error","code":"...",...} -> request/channel error
    //   {"event":"notice","code":"64008"}  -> service upgrade: reconnect now
    if (payload.find("\"notice\"") != std::string::npos
        && payload.find("64008") != std::string::npos) {
        // OKX announces it will close the connection soon. Do not keep
        // pretending the session is healthy: count the notice and flag the
        // session for teardown. IXWebSocket::stop() joins the very callback
        // thread this code runs on, so the actual stop() MUST be performed by
        // the liveness worker; doing it here would self-join and terminate.
        ++upgradeNotices_;
        {
            const std::lock_guard lock(mutex_);
            closeRequested_ = true;
        }
        cv_.notify_all();
        return;
    }

    if (payload.find("\"error\"") != std::string::npos
        || payload.find("\"warning\"") != std::string::npos) {
        ++errorEvents_;
    }
}

void OkxWebSocketSource::runWorker()
{
    auto localHandle = std::make_unique<IxWebSocketHandle>();
    localHandle->socket.setUrl(config_.url);

    // IXWebSocket silently re-handshakes on its own by default. Reconnect
    // policy is owned exclusively by FeedClient: a dropped transport must
    // surface as one Disconnected event and let the worker retire, never as
    // an untracked library-internal second session.
    localHandle->socket.disableAutomaticReconnection();

    if (!config_.caCertPath.empty()) {
        ix::SocketTLSOptions tlsOptions;
        tlsOptions.caFile = config_.caCertPath;
        localHandle->socket.setTLSOptions(tlsOptions);
    }

    localHandle->socket.setOnMessageCallback(
        [this](const ix::WebSocketMessagePtr& message) {
            switch (message->type) {
            case ix::WebSocketMessageType::Open: {
                lastActivityNs_ = steadyNowNs();

                std::string subscribeMessage;
                {
                    const std::lock_guard lock(mutex_);
                    connected_ = true;
                    pingOutstanding_ = false;
                    subscribeMessage = "{\"op\":\"subscribe\",\"args\":[{"
                        "\"channel\":\"" + config_.channel
                        + "\",\"instId\":\"" + config_.instrument + "\"}]}";
                }
                sendText(subscribeMessage);
                emitState(ConnectionState::Connected);
                break;
            }

            case ix::WebSocketMessageType::Message: {
                lastActivityNs_ = steadyNowNs();

                const std::string& payload = message->str;
                if (payload == "pong") {
                    const std::lock_guard lock(mutex_);
                    pingOutstanding_ = false;
                    break;
                }

                if (looksLikeControlEvent(payload)) {
                    handleControlEvent(payload);
                    break;
                }

                emitFrame(payload);
                break;
            }

            case ix::WebSocketMessageType::Close:
            case ix::WebSocketMessageType::Error: {
                // Whether a clean close or a failed handshake, the session is
                // over; FeedClient owns what happens next (backoff reconnect).
                // Mutate shared state under mutex_ (the liveness worker reads
                // it there), then notify OUTSIDE the lock: state handlers may
                // re-enter resubscribe(), which takes this same mutex.
                {
                    const std::lock_guard lock(mutex_);
                    connected_ = false;
                    pingOutstanding_ = false;
                    sessionTerminal_ = true;
                }
                cv_.notify_all();
                emitState(ConnectionState::Disconnected);
                break;
            }

            case ix::WebSocketMessageType::Ping:
            case ix::WebSocketMessageType::Pong:
            case ix::WebSocketMessageType::Fragment:
            default:
                lastActivityNs_ = steadyNowNs();
                break;
            }
        });

    {
        const std::lock_guard lock(mutex_);
        handle_ = std::move(localHandle);
    }

    handle_->socket.start();

    // Liveness loop: application-level OKX ping/pong supervision. The loop
    // ends when disconnect() is requested OR the transport reached its
    // terminal event - a dead session has nothing left to supervise, so the
    // worker retires and lets connect() start a fresh physical connection.
    {
        std::unique_lock lock(mutex_);
        while (!stopRequested_ && !sessionTerminal_) {
            cv_.wait_for(lock, config_.livenessTick);
            if (stopRequested_ || sessionTerminal_ || closeRequested_) {
                break;
            }
            if (!connected_) {
                continue;
            }
            const auto now = steadyNowNs();
            const auto silence = std::chrono::nanoseconds(now - lastActivityNs_.load());

            if (silence < config_.pingInterval) {
                continue;
            }

            if (!pingOutstanding_) {
                pingOutstanding_ = true;
                lock.unlock();
                sendText("ping");
                lock.lock();
            } else if (silence >= config_.pingInterval + config_.pongTimeout) {
                // No pong inside the deadline: connection is unhealthy.
                ++missedPongs_;
                lock.unlock();
                handle_->socket.stop(1000, "pong timeout");
                lock.lock();
            }
        }
    }

    // Teardown runs on THIS thread only: IXWebSocket::stop() joins its
    // internal callback thread, so it must never be invoked from a message
    // callback. socket.stop() below may synchronously deliver the final Close
    // callback - mutex_ is deliberately not held here.
    handle_->socket.stop();
    const std::lock_guard lock(mutex_);
    handle_.reset();
    workerRunning_ = false;
}

} // namespace qx::feed
