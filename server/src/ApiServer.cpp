#include <nlohmann/json.hpp>

#include <stdexcept>
#include <thread>

#include "qx/server/ApiServer.h"
#include "qx/server/WebSocket.h"

namespace qx::server {

namespace {

std::string jsonError(int status, const std::string& message)
{
    return "{\"status\":" + std::to_string(status) + ",\"error\":\"" + message + "\"}";
}

} // namespace

void ApiServer::updateEngineState(const EngineView& view)
{
    const std::lock_guard lock(mutex_);

    qx::MarketEvent snapshot;
    snapshot.type = qx::EventType::Snapshot;
    snapshot.sequence = view.lastSequence;
    snapshot.bids = view.book.bids();
    snapshot.asks = view.book.asks();

    state_.book.applySnapshot(snapshot);
    state_.health = view.health;
    state_.lastSequence = view.lastSequence;
}

void ApiServer::attach(HttpServer& http)
{
    http.setHandler([this](const HttpRequest& request) { return handleRequest(request); });
    http.setWebSocketHandler([this](int fd) { runEventStream(fd); });
}

HttpResponse ApiServer::handleRequest(const HttpRequest& request)
{
    if (request.method == "GET" && request.path == "/health") {
        const std::lock_guard lock(mutex_);
        HttpResponse response;
        response.body = healthToJson(state_.health);
        return response;
    }

    if (request.method == "GET" && request.path == "/book") {
        const std::lock_guard lock(mutex_);
        HttpResponse response;
        response.body = bookToJson(state_.book);
        return response;
    }

    if (request.method == "POST" && request.path == "/simulate") {
        nlohmann::json parsed;
        try {
            parsed = nlohmann::json::parse(request.body);
        } catch (...) {
            HttpResponse response;
            response.status = 400;
            response.body = jsonError(400, "body is not valid JSON");
            return response;
        }

        qx::OrderRequest order;
        try {
            const std::string side = parsed.at("side").get<std::string>();
            const std::string mode = parsed.at("mode").get<std::string>();
            order.size = parsed.at("size").get<double>();
            order.takerFeeBps = parsed.value("feeBps", 0.0);

            order.side = side == "buy" ? qx::Side::Buy
                : side == "sell"       ? qx::Side::Sell
                                       : throw std::runtime_error("side must be buy or sell");

            order.sizeMode = mode == "notional" ? qx::SizeMode::Notional
                : mode == "base"                ? qx::SizeMode::BaseQuantity
                                                : throw std::runtime_error(
                                                    "mode must be notional or base");

            if (!(order.size > 0.0)) {
                throw std::runtime_error("size must be positive");
            }
        } catch (...) {
            HttpResponse response;
            response.status = 400;
            response.body = jsonError(400,
                "expected {\"side\":\"buy\"|\"sell\",\"mode\":\"notional\"|\"base\","
                "\"size\":number,\"feeBps\":number}");
            return response;
        }

        const std::lock_guard lock(mutex_);
        HttpResponse response;
        response.body = resultToJson(qx::ExecutionSimulator {}.execute(state_.book, order));
        return response;
    }

    if (request.method == "GET" || request.method == "POST") {
        HttpResponse response;
        response.status = 404;
        response.body = jsonError(404, "unknown path");
        return response;
    }

    HttpResponse response;
    response.status = 400;
    response.body = jsonError(400, "malformed request");
    return response;
}

std::string ApiServer::bookToJson(const qx::Book& book)
{
    nlohmann::json out;
    out["sequence"] = book.lastSequence();

    auto levelsToArray = [](const std::vector<qx::Level>& levels) {
        std::vector<nlohmann::json> rows;
        rows.reserve(levels.size());
        for (const auto& level : levels) {
            rows.push_back(nlohmann::json { { "price", level.price }, { "size", level.size } });
        }
        return rows;
    };

    out["bids"] = levelsToArray(book.bids());
    out["asks"] = levelsToArray(book.asks());

    if (const auto mid = book.mid()) {
        out["mid"] = *mid;
    }
    if (const auto spread = book.spreadBps()) {
        out["spreadBps"] = *spread;
    }
    return out.dump();
}

std::string ApiServer::healthToJson(const qx::feed::FeedHealth& health)
{
    nlohmann::json out;
    out["connected"] = health.state == qx::feed::ConnectionState::Connected;
    out["bookReady"] = health.bookReady;
    out["stale"] = health.stale;
    out["reconnects"] = health.reconnects;
    out["sequenceGaps"] = health.sequenceGaps;
    out["malformedMessages"] = health.malformedMessages;
    out["staleRejected"] = health.staleRejected;
    out["checksumFailures"] = health.checksumFailures;
    out["messagesAccepted"] = health.messagesAccepted;
    out["lastMessageAgeMs"] = health.lastMessageAgeMs;
    return out.dump();
}

std::string ApiServer::resultToJson(const qx::ExecutionResult& result)
{
    nlohmann::json out;
    out["side"] = result.side == qx::Side::Buy ? "buy" : "sell";
    out["requestedNotional"] = result.requestedNotional;
    out["requestedBaseQty"] = result.requestedBaseQty;
    out["filledNotional"] = result.filledNotional;
    out["filledBaseQty"] = result.filledBaseQty;
    out["referenceMid"] = result.referenceMid;
    out["bestPrice"] = result.bestPrice;
    out["vwap"] = result.executionVwap;
    out["spreadBps"] = result.spreadBps;
    out["slippageBps"] = result.slippageBps;
    out["slippageUsd"] = result.slippageUsd;
    out["feeBps"] = result.feeBps;
    out["feeUsd"] = result.feeUsd;
    out["totalCostBps"] = result.totalCostBps;
    out["totalCostUsd"] = result.totalCostUsd;
    out["levelsConsumed"] = result.levelsConsumed;
    out["insufficientLiquidity"] = result.insufficientLiquidity;
    return out.dump();
}

void ApiServer::runEventStream(int fd)
{
    while (true) {
        std::string payload;
        {
            const std::lock_guard lock(mutex_);

            nlohmann::json message;
            message["sequence"] = state_.book.lastSequence();
            message["health"] = nlohmann::json::parse(healthToJson(state_.health));

            if (const auto mid = state_.book.mid()) {
                message["mid"] = *mid;
            }
            if (const auto spread = state_.book.spreadBps()) {
                message["spreadBps"] = *spread;
            }

            payload = message.dump();
        }

        if (!webSocketSendText(fd, payload)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

} // namespace qx::server
