#include "qx/server/WebSocket.h"

#include <sys/socket.h>

namespace qx::server {

namespace {

bool sendAllBytes(int fd, const void* data, std::size_t length)
{
    const char* bytes = static_cast<const char*>(data);
    std::size_t sent = 0;
    while (sent < length) {
        const ssize_t written = ::send(fd, bytes + sent, length - sent, MSG_NOSIGNAL);
        if (written <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(written);
    }
    return true;
}

} // namespace

bool webSocketSendText(int fd, const std::string& payload)
{
    std::string frame;
    frame.reserve(payload.size() + 10);

    frame.push_back(static_cast<char>(0x81u));

    if (payload.size() <= 125u) {
        frame.push_back(static_cast<char>(payload.size()));
    } else if (payload.size() <= 65535u) {
        frame.push_back(static_cast<char>(126u));
        frame.push_back(static_cast<char>((payload.size() >> 8) & 0xFFu));
        frame.push_back(static_cast<char>(payload.size() & 0xFFu));
    } else {
        frame.push_back(static_cast<char>(127u));
        for (int shift = 56; shift >= 0; shift -= 8) {
            frame.push_back(
                static_cast<char>((static_cast<std::uint64_t>(payload.size()) >> shift) & 0xFFu));
        }
    }

    frame.append(payload);
    return sendAllBytes(fd, frame.data(), frame.size());
}

} // namespace qx::server
