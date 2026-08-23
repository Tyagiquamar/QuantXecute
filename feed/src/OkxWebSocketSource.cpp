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
    bool startNeeded = false;
    {
        const std::lock_guard lock(mutex_);
        if (!workerRunning_) {
            workerRunning_ = true;
            stopRequested_ = false;
            connected_ = false;
            pingOutstanding_ = false;
            startNeeded = true;
        }
    }

    if (!startNeeded) {
        // Physical connect is idempotent while a session is being established.
        return;
    }

    emitState(ConnectionState::Connecting);
    worker_ = std::thread([this] { runWorker(); });
}

void OkxWebSocketSource::disconnect()
{
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
        // pretending the session is healthy: count the notice, drop the
        // connection cleanly and let FeedClient's backoff reconnect.
        ++upgradeNotices_;
        const std::lock_guard lock(mutex_);
        if (handle_) {
            handle_->socket.stop(1000, "service upgrade");
        }
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

    if (!config_.caCertPath.empty()) {
        ix::SocketTLSOptions tlsOptions;
        tlsOptions.caFile = config_.caCertPath;
        localHandle->socket.setTLSOptions(tlsOptions);
    }

    auto weakSourceState = [this]() { return !stopRequested_; };

    localHandle->socket.setOnMessageCallback(
        [this](const ix::WebSocketMessagePtr& message) {
            switch (message->type) {
            case ix::WebSocketMessageType::Open: {
                lastActivityNs_ = steadyNowNs();
                pingOutstanding_ = false;

                std::string subscribeMessage;
                {
                    const std::lock_guard lock(mutex_);
                    connected_ = true;
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
                connected_ = false;
                pingOutstanding_ = false;
                // Whether a clean close or a failed handshake, the session is
                // over; FeedClient owns what happens next (backoff reconnect).
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

    // Liveness loop: application-level OKX ping/pong supervision.
    {
        std::unique_lock lock(mutex_);
        while (!stopRequested_) {
            cv_.wait_for(lock, config_.livenessTick);
            if (stopRequested_) {
                break;
            }
            const bool isConnected = connected_;
            const auto now = steadyNowNs();
            const auto silence = std::chrono::nanoseconds(now - lastActivityNs_.load());

            if (!isConnected || silence < config_.pingInterval) {
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

    handle_->socket.stop();
    const std::lock_guard lock(mutex_);
    handle_.reset();
    workerRunning_ = false;
}

} // namespace qx::feed