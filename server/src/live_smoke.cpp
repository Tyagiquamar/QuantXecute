// Manual live-smoke verification against the public OKX endpoint.
//
// Connects, subscribes to books, and checks that a real snapshot plus
// incremental updates arrive with valid seqId/prevSeqId continuity.
// Intentionally NOT part of automated CI: live network tests would make PR
// verification depend on exchange uptime and geography.
//
// Usage: quantxecute-live-smoke [wss-url] [instrument]

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "qx/feed/FeedClient.h"
#include "qx/feed/OkxWebSocketSource.h"

int main(int argc, char** argv)
{
    qx::feed::OkxWebSocketSource::Config config;
    if (argc > 1) {
        config.url = argv[1];
    }
    if (argc > 2) {
        config.instrument = argv[2];
    }

    std::printf("connecting to %s (%s %s)\n", config.url.c_str(),
        config.channel.c_str(), config.instrument.c_str());

    qx::feed::OkxWebSocketSource source(config);
    qx::feed::FeedClient client(source);

    client.start();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
    std::uint64_t acceptedAtLastCheck = 0;

    while (std::chrono::steady_clock::now() < deadline) {
        client.tick();
        const auto health = client.health();
        if (health.messagesAccepted != acceptedAtLastCheck) {
            acceptedAtLastCheck = health.messagesAccepted;
            std::printf("[%lld ms] accepted=%llu seq=%llu gaps=%llu malformed=%llu\n",
                static_cast<long long>(health.lastMessageAgeMs),
                static_cast<unsigned long long>(health.messagesAccepted),
                static_cast<unsigned long long>(client.book().lastSequence()),
                static_cast<unsigned long long>(health.sequenceGaps),
                static_cast<unsigned long long>(health.malformedMessages));
        }

        // Success criteria: snapshot applied (non-empty book), at least one
        // subsequent incremental update, no continuity failures.
        if (client.isBookReady() && !client.book().empty()
            && health.messagesAccepted >= 3 && health.sequenceGaps == 0) {
            std::printf("PASS: snapshot + incremental updates with clean "
                        "seqId/prevSeqId continuity\n");
            client.stop();
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    const auto health = client.health();
    std::fprintf(stderr,
        "FAIL: no verified book in time (accepted=%llu connected=%d gaps=%llu "
        "malformed=%llu)\n",
        static_cast<unsigned long long>(health.messagesAccepted),
        health.state == qx::feed::ConnectionState::Connected ? 1 : 0,
        static_cast<unsigned long long>(health.sequenceGaps),
        static_cast<unsigned long long>(health.malformedMessages));
    client.stop();
    return 1;
}