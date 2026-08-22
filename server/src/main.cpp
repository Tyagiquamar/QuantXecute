#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#include "qx/Replay.h"
#include "qx/feed/FeedClient.h"
#include "qx/feed/OkxWebSocketSource.h"
#include "qx/server/ApiServer.h"
#include "qx/server/HttpServer.h"

namespace {

enum class EngineMode { Replay, Live };

std::string envOr(const char* name, const std::string& fallback)
{
    const char* value = std::getenv(name);
    return (value != nullptr && *value != '\0') ? std::string(value) : fallback;
}

std::atomic<bool> gShutdown { false };

void handleSignal(int)
{
    gShutdown = true;
}

EngineMode parseMode(const std::string& mode)
{
    return mode == "live" ? EngineMode::Live : EngineMode::Replay;
}

} // namespace

int main(int argc, char** argv)
{
    int port = std::stoi(envOr("PORT", "8080"));
    std::string bindAddress = "0.0.0.0";
    std::string logPath;
    std::string modeArg = envOr("QX_MODE", "");
    std::string exchange = envOr("QX_EXCHANGE", "okx");
    std::string instrument = envOr("QX_INSTRUMENT", "BTC-USDT");
    std::string channel = envOr("QX_CHANNEL", "books");
    std::string wsUrl = envOr("QX_OKX_WS_URL", "wss://ws.okx.com:8443/ws/v5/public");
    std::string caCertPath = envOr("QX_CA_CERT_PATH", "");

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        const auto value = [argc, argv, &i]() -> std::string {
            if (i + 1 >= argc) {
                return "";
            }
            return argv[++i];
        };

        if (argument == "--port" || argument == "-p") {
            port = std::stoi(value());
        } else if (argument == "--bind" || argument == "-b") {
            bindAddress = value();
        } else if (argument == "--log" || argument == "-l") {
            logPath = value();
            modeArg = "replay";
        } else if (argument == "--mode" || argument == "-m") {
            modeArg = value();
        } else if (argument == "--exchange") {
            exchange = value();
        } else if (argument == "--instrument" || argument == "--instId") {
            instrument = value();
        } else if (argument == "--channel") {
            channel = value();
        } else if (argument == "--ws-url") {
            wsUrl = value();
        }
    }

    if (modeArg.empty()) {
        // Explicit configuration only: never guess between demo and live.
        std::fprintf(stderr,
            "no engine mode configured; pass --mode replay --log <file> "
            "(or QX_MODE=replay) for fixture replay, or --mode live for OKX\n");
        return 2;
    }

    const EngineMode mode = parseMode(modeArg);
    if (mode == EngineMode::Replay && logPath.empty()) {
        logPath = envOr("QX_LOG", "");
    }

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    const std::string allowedOrigins = envOr("QX_ALLOWED_ORIGINS", "");

    qx::server::ApiServer api;
    api.setAllowedOrigins(allowedOrigins);
    qx::server::HttpServer http;
    api.attach(http);

    std::thread feedThread;

    qx::feed::FeedConfig feedConfig;
    feedConfig.stalenessThreshold = std::chrono::milliseconds(
        std::stoi(envOr("QX_STALENESS_MS", "5000")));

    std::unique_ptr<qx::feed::OkxWebSocketSource> okxSource;
    std::unique_ptr<qx::feed::FeedClient> client;

    if (mode == EngineMode::Live) {
        if (exchange != "okx") {
            std::fprintf(stderr, "unsupported live exchange: %s (only okx)\n",
                exchange.c_str());
            return 2;
        }

        qx::feed::OkxWebSocketSource::Config sourceConfig;
        sourceConfig.url = wsUrl;
        sourceConfig.channel = channel;
        sourceConfig.instrument = instrument;
        sourceConfig.caCertPath = caCertPath;

        okxSource = std::make_unique<qx::feed::OkxWebSocketSource>(sourceConfig);
        client = std::make_unique<qx::feed::FeedClient>(*okxSource, feedConfig);

        api.describeEngine("live", exchange, instrument, channel);

        std::printf("[startup] mode=live exchange=%s instrument=%s channel=%s url=%s\n",
            exchange.c_str(), instrument.c_str(), channel.c_str(), wsUrl.c_str());
        std::fflush(stdout);
    } else {
        if (logPath.empty()) {
            std::fprintf(stderr,
                "replay mode requires --log <file> (or QX_LOG) pointing at a "
                "recorded event log\n");
            return 2;
        }
        api.describeEngine("replay", "fixture", "", "");

        std::printf("[startup] mode=replay log=%s\n", logPath.c_str());
        std::fflush(stdout);
    }

    feedThread = std::thread([&] {
        if (mode == EngineMode::Live) {
            client->start();
            while (!gShutdown.load()) {
                client->tick();
                qx::server::ApiServer::EngineView view;
                const auto bookView = client->book();
                view.bids = bookView.bidLevels;
                view.asks = bookView.askLevels;
                view.lastSequence = bookView.lastSequence;
                view.health = client->health();
                api.updateEngineState(view);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            client->stop();
        } else {
            while (!gShutdown.load()) {
                qx::Book book;
                const auto stats = qx::replayInto(book, logPath);

                qx::server::ApiServer::EngineView view;
                view.bids = book.bids();
                view.asks = book.asks();
                view.lastSequence = stats.lastSequence;
                view.health.bookReady = stats.eventsApplied > 0;
                view.health.messagesAccepted = stats.eventsApplied;
                view.health.state = qx::feed::ConnectionState::Connected;
                api.updateEngineState(view);

                // Replay is an explicitly selected deterministic demo: loop
                // it until shutdown rather than serving an empty engine.
                for (int i = 0; i < 100 && !gShutdown.load(); ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
        }
    });

    if (!http.start(bindAddress, port)) {
        std::fprintf(stderr, "failed to bind %s:%d\n", bindAddress.c_str(), port);
        gShutdown = true;
        feedThread.join();
        return 1;
    }

    std::printf("[startup] listening on %s:%d\n", bindAddress.c_str(), port);
    std::fflush(stdout);

    while (!gShutdown.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::printf("[shutdown] stopping HTTP server and feed pipeline\n");
    std::fflush(stdout);

    http.stop();
    feedThread.join();

    if (client) {
        client.reset();
    }
    if (okxSource) {
        okxSource.reset();
    }

    std::printf("[shutdown] clean exit\n");
    return 0;
}