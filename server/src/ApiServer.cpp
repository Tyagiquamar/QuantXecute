#include <cctype>
#include <cmath>

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <thread>
#include <vector>

#include "qx/server/ApiServer.h"
#include "qx/server/WebSocket.h"

namespace qx::server {

namespace {

std::string jsonError(int status, const std::string& message)
{
    return "{\"status\":" + std::to_string(status) + ",\"error\":\"" + message + "\"}";
}

std::string toLower(std::string value)
{
    for (auto& character : value) {
        character = static_cast<char>(::tolower(character));
    }
    return value;
}

} // namespace

void ApiServer::describeEngine(const std::string& mode,
    const std::string& exchange,
    const std::string& instrument,
    const std::string& channel)
{
    const std::lock_guard lock(mutex_);
    mode_ = mode;
    exchange_ = exchange;
    instrument_ = instrument;
    channel_ = channel;
}

void ApiServer::setAllowedOrigins(const std::string& csvOrigins)
{
    std::vector<std::string> origins;
    std::string current;
    for (const char character : csvOrigins) {
        if (character == ',') {
            if (!current.empty()) {
                origins.push_back(current);
            }
            current.clear();
        } else if (character != ' ') {
            current.push_back(character);
        }
    }
    if (!current.empty()) {
        origins.push_back(current);
    }

    const std::lock_guard lock(mutex_);
    allowedOrigins_ = std::move(origins);
}

void ApiServer::updateEngineState(const EngineView& view)
{
    const std::lock_guard lock(mutex_);

    state_ = view;

    qx::MarketEvent snapshot;
    snapshot.type = qx::EventType::Snapshot;
    snapshot.sequence = view.lastSequence;
    snapshot.bids = view.bids;
    snapshot.asks = view.asks;
    book_.applySnapshot(snapshot);
}

void ApiServer::attach(HttpServer& http)
{
    http.setHandler([this](const HttpRequest& request) { return handleRequest(request); });
    http.setWebSocketHandler([this](int fd) { runEventStream(fd); });
}

bool ApiServer::originAllowed(const std::string& origin) const
{
    if (allowedOrigins_.empty() || origin.empty()) {
        return false;
    }
    const std::string lowered = toLower(origin);
    for (const auto& candidate : allowedOrigins_) {
        if (lowered == toLower(candidate)) {
            return true;
        }
    }
    return false;
}

void ApiServer::applyCors(HttpResponse& response, const HttpRequest& request) const
{
    // Narrow allowlist: only explicitly configured origins (plus localhost
    // development hosts) receive CORS grants; never a blanket "*".
    const std::string origin = request.header("origin");
    if (origin.empty()) {
        return;
    }

    bool allowed = originAllowed(origin);
    if (!allowed) {
        const std::string lowered = toLower(origin);
        allowed = lowered.rfind("http://localhost", 0) == 0
            || lowered.rfind("http://127.0.0.1", 0) == 0;
    }

    if (allowed) {
        response.extraHeaders["Access-Control-Allow-Origin"] = origin;
        response.extraHeaders["Vary"] = "Origin";
        response.extraHeaders["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS";
        response.extraHeaders["Access-Control-Allow-Headers"] = "Content-Type";
    }
}

HttpResponse ApiServer::handleRequest(const HttpRequest& request)
{
    HttpResponse response;

    if (request.method == "OPTIONS") {
        response.status = 204;
        response.body = "";
        applyCors(response, request);
        return response;
    }

    if (request.method == "GET" && request.path == "/health") {
        const std::lock_guard lock(mutex_);
        nlohmann::json out;
        out["mode"] = mode_;
        out["exchange"] = exchange_;
        out["instrument"] = instrument_;
        out["channel"] = channel_;

        const auto& health = state_.health;
        out["connected"] = health.state == qx::feed::ConnectionState::Connected;
        out["bookReady"] = health.bookReady;
        out["stale"] = health.stale;
        out["sequenceGaps"] = health.sequenceGaps;
        out["reconnects"] = health.reconnects;
        out["malformedMessages"] = health.malformedMessages;
        out["staleRejected"] = health.staleRejected;
        out["checksumFailures"] = health.checksumFailures;
        out["messagesAccepted"] = health.messagesAccepted;
        out["lastSeqId"] = state_.lastSequence;
        out["lastMessageAgeMs"] = health.lastMessageAgeMs;
        out["integrity"] = "seqId/prevSeqId";

        response.body = out.dump();
        applyCors(response, request);
        return response;
    }
    if (request.method == "GET" && request.path == "/book") {
        const std::lock_guard lock(mutex_);
        response.body = bookToJson(book_);
        applyCors(response, request);
        return response;
    }

    if (request.method == "POST" && request.path == "/simulate") {
        nlohmann::json parsed;
        try {
            parsed = nlohmann::json::parse(request.body);
        } catch (...) {
            response.status = 400;
            response.body = jsonError(400, "body is not valid JSON");
            applyCors(response, request);
            return response;
        }

        qx::OrderRequest order;
        try {
            const std::string side = parsed.at("side").get<std::string>();
            const std::string mode = parsed.at("mode").get<std::string>();
            order.size = parsed.at("size").get<double>();
            order.takerFeeBps = parsed.value("feeBps", 0.0);

            if (!std::isfinite(order.size)) {
                throw std::runtime_error("size must be finite");
            }

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
            response.status = 400;
            response.body = jsonError(400,
                "expected {\"side\":\"buy\"|\"sell\",\"mode\":\"notional\"|\"base\","
                "\"size\":number,\"feeBps\":number}");
            applyCors(response, request);
            return response;
        }

        const std::lock_guard lock(mutex_);
        if (!state_.health.bookReady) {
            // Never simulate against an unavailable or invalidated book.
            response.status = 503;
            response.body = jsonError(503, "engine book is not ready");
            applyCors(response, request);
            return response;
        }

        response.body = resultToJson(qx::ExecutionSimulator {}.execute(book_, order));
        applyCors(response, request);
        return response;
    }

    if (request.method == "GET" || request.method == "POST") {
        response.status = 404;
        response.body = jsonError(404, "unknown path");
        applyCors(response, request);
        return response;
    }

    response.status = 400;
    response.body = jsonError(400, "malformed request");
    applyCors(response, request);
    return response;
}std::string ApiServer::bookToJson(const qx::Book& book)
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

            const auto& health = state_.health;

            nlohmann::json healthMessage;
            healthMessage["connected"] = health.state == qx::feed::ConnectionState::Connected;
            healthMessage["bookReady"] = health.bookReady;
            healthMessage["stale"] = health.stale;
            healthMessage["reconnects"] = health.reconnects;
            healthMessage["sequenceGaps"] = health.sequenceGaps;
            healthMessage["malformedMessages"] = health.malformedMessages;
            healthMessage["staleRejected"] = health.staleRejected;
            healthMessage["checksumFailures"] = health.checksumFailures;
            healthMessage["messagesAccepted"] = health.messagesAccepted;
            healthMessage["lastSeqId"] = state_.lastSequence;
            healthMessage["lastMessageAgeMs"] = health.lastMessageAgeMs;

            nlohmann::json message;
            message["mode"] = mode_;
            message["exchange"] = exchange_;
            message["instrument"] = instrument_;
            message["channel"] = channel_;
            message["sequence"] = state_.lastSequence;
            message["health"] = std::move(healthMessage);

            if (const auto mid = book_.mid()) {
                message["mid"] = *mid;
            }
            if (const auto spread = book_.spreadBps()) {
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