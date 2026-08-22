#pragma once

#include <atomic>
#include <mutex>

#include "qx/Book.h"
#include "qx/feed/FeedClient.h"
#include "qx/server/HttpServer.h"

namespace qx::server {

class ApiServer {
public:
    struct EngineView {
        qx::Book book;
        qx::feed::FeedHealth health;
        std::uint64_t lastSequence = 0;
    };

    void updateEngineState(const EngineView& view);

    void attach(HttpServer& http);

private:
    HttpResponse handleRequest(const HttpRequest& request);
    void runEventStream(int fd);

    static std::string bookToJson(const qx::Book& book);
    static std::string healthToJson(const qx::feed::FeedHealth& health);
    static std::string resultToJson(const qx::ExecutionResult& result);

    mutable std::mutex mutex_;
    EngineView state_;
};

} // namespace qx::server
