#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "qx/Book.h"
#include "qx/Level.h"
#include "qx/feed/FeedClient.h"
#include "qx/server/HttpServer.h"

namespace qx::server {

class ApiServer {
public:
    // Snapshot of engine state pushed by whichever driver (replay loop or
    // live feed client) owns the pipeline.
    struct EngineView {
        std::vector<qx::Level> bids;
        std::vector<qx::Level> asks;
        std::uint64_t lastSequence = 0;
        qx::feed::FeedHealth health;
    };

    // Human-facing engine identity surfaced on /health. mode is "live" or
    // "replay"; exchange is "okx" in live mode, "fixture" in replay.
    void describeEngine(const std::string& mode,
        const std::string& exchange,
        const std::string& instrument,
        const std::string& channel);

    void updateEngineState(const EngineView& view);

    // Narrow CORS: comma-separated origin allowlist (empty disables CORS
    // headers entirely). Localhost origins are always honored for dev.
    void setAllowedOrigins(const std::string& csvOrigins);

    void attach(HttpServer& http);

private:
    HttpResponse handleRequest(const HttpRequest& request);
    void runEventStream(int fd);
    bool originAllowed(const std::string& origin) const;
    void applyCors(HttpResponse& response, const HttpRequest& request) const;

    static std::string bookToJson(const qx::Book& book);
    static std::string resultToJson(const qx::ExecutionResult& result);

    mutable std::mutex mutex_;
    EngineView state_;
    qx::Book book_;

    std::string mode_ = "unknown";
    std::string exchange_;
    std::string instrument_;
    std::string channel_;
    std::vector<std::string> allowedOrigins_;
};

} // namespace qx::server