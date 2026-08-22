#pragma once

#include <functional>
#include <string_view>

namespace qx::feed {

enum class ConnectionState : std::uint8_t {
    Disconnected,
    Connecting,
    Connected,
};

class FeedSource {
public:
    using FrameHandler = std::function<void(std::string_view)>;
    using StateHandler = std::function<void(ConnectionState)>;

    virtual ~FeedSource() = default;

    virtual void connect() = 0;
    virtual void disconnect() = 0;
    virtual void resubscribe() {}

    void onFrame(FrameHandler handler) { frameHandler_ = std::move(handler); }
    void onStateChange(StateHandler handler) { stateHandler_ = std::move(handler); }

protected:
    void emitFrame(const std::string_view payload)
    {
        if (frameHandler_) {
            frameHandler_(payload);
        }
    }

    void emitState(ConnectionState state)
    {
        if (stateHandler_) {
            stateHandler_(state);
        }
    }

private:
    FrameHandler frameHandler_;
    StateHandler stateHandler_;
};

} // namespace qx::feed
