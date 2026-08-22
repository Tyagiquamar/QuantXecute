#pragma once

#include <string>
#include <vector>

#include "qx/feed/FeedSource.h"

namespace qx::feed::test {

class MockFeedSource final : public FeedSource {
public:
    void connect() override
    {
        ++connectCalls;
        emitState(ConnectionState::Connecting);
        if (autoConnect) {
            emitState(ConnectionState::Connected);
        }
    }

    void disconnect() override
    {
        ++disconnectCalls;
        connected = false;
        emitState(ConnectionState::Disconnected);
    }

    void resubscribe() override { ++resubscribeCalls; }

    void remoteDisconnect()
    {
        connected = false;
        emitState(ConnectionState::Disconnected);
    }

    void deliver(std::string_view payload) { emitFrame(payload); }

    std::vector<std::string> sent;
    int connectCalls = 0;
    int disconnectCalls = 0;
    int resubscribeCalls = 0;
    bool autoConnect = true;
    bool connected = false;
};

} // namespace qx::feed::test
