#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "qx/Replay.h"
#include "qx/feed/FeedClient.h"
#include "qx/server/ApiServer.h"
#include "qx/server/HttpServer.h"

int main(int argc, char** argv)
{
    int port = 8080;
    std::string bindAddress = "0.0.0.0";
    std::string logPath;

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
        }
    }

    qx::server::ApiServer api;
    qx::server::HttpServer http;
    api.attach(http);

    std::thread engineThread([&api, logPath] {
        while (true) {
            if (!logPath.empty()) {
                qx::Book book;
                const auto stats = qx::replayInto(book, logPath);

                qx::server::ApiServer::EngineView view;
                view.health.bookReady = stats.eventsApplied > 0;
                view.health.messagesAccepted = stats.eventsApplied;
                view.health.state = qx::feed::ConnectionState::Connected;
                view.lastSequence = stats.lastSequence;

                qx::MarketEvent snapshot;
                snapshot.type = qx::EventType::Snapshot;
                snapshot.sequence = stats.lastSequence;
                snapshot.bids = book.bids();
                snapshot.asks = book.asks();
                view.book.applySnapshot(snapshot);

                api.updateEngineState(view);
            }

            std::this_thread::sleep_for(std::chrono::seconds(10));
        }
    });

    if (!http.start(bindAddress, port)) {
        std::fprintf(stderr, "failed to bind %s:%d\n", bindAddress.c_str(), port);
        return 1;
    }

    std::printf("quantxecute-server listening on %s:%d\n", bindAddress.c_str(), port);
    if (!logPath.empty()) {
        std::printf("replaying fixture: %s\n", logPath.c_str());
    } else {
        std::printf("no --log given; serving empty engine state\n");
    }

    engineThread.join();
    return 0;
}
