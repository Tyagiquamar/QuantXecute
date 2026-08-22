#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string>

#include <nlohmann/json.hpp>

#include "qx/server/ApiServer.h"

namespace {

qx::Level level(double price, double size)
{
    return qx::Level { price, size };
}

void primeReplayBook(qx::server::ApiServer& api)
{
    api.describeEngine("replay", "fixture", "", "");

    qx::server::ApiServer::EngineView view;
    view.bids = { level(100.0, 5.0), level(99.5, 3.0) };
    view.asks = { level(100.5, 4.0), level(101.0, 2.0) };
    view.lastSequence = 19;
    view.health.bookReady = true;
    view.health.state = qx::feed::ConnectionState::Connected;
    view.health.messagesAccepted = 4;
    view.health.lastMessageAgeMs = 12;
    api.updateEngineState(view);
}

nlohmann::json bodyAsJson(const qx::server::HttpResponse& response)
{
    return nlohmann::json::parse(response.body);
}

} // namespace

TEST_CASE("/health reports mode identity and integrity counters")
{
    qx::server::ApiServer api;
    primeReplayBook(api);

    const auto response = api.handleRequest({ "GET", "/health", "", {} });
    CHECK(response.status == 200);

    const auto health = bodyAsJson(response);
    CHECK(health["mode"] == "replay");
    CHECK(health["exchange"] == "fixture");
    CHECK(health["connected"] == true);
    CHECK(health["bookReady"] == true);
    CHECK(health["lastSeqId"] == 19);
    CHECK(health["sequenceGaps"] == 0);
    CHECK(health["malformedMessages"] == 0);
}

TEST_CASE("/book serves the current ladder")
{
    qx::server::ApiServer api;
    primeReplayBook(api);

    const auto book = bodyAsJson(api.handleRequest({ "GET", "/book", "", {} }));
    REQUIRE(book["bids"].size() == 2);
    REQUIRE(book["asks"].size() == 2);
    CHECK(book["sequence"] == 19);
    CHECK(book["bids"][0]["price"] == 100.0);
    CHECK(book["asks"][0]["price"] == 100.5);
    CHECK(book.contains("mid"));
}

TEST_CASE("valid /simulate executes against the served book")
{
    qx::server::ApiServer api;
    primeReplayBook(api);

    const auto result = bodyAsJson(api.handleRequest({ "POST", "/simulate",
        R"({"side":"buy","mode":"notional","size":25000,"feeBps":5})", {} }));

    CHECK(result["side"] == "buy");
    CHECK(result["filledBaseQty"] > 0.0);
    CHECK(result["filledNotional"] > 0.0);
    CHECK(result["vwap"] > 0.0);
    CHECK(result["insufficientLiquidity"] == false);
    CHECK(result["totalCostUsd"] > 0.0);
}

TEST_CASE("malformed /simulate returns structured 400s")
{
    qx::server::ApiServer api;
    primeReplayBook(api);

    SUBCASE("invalid json")
    {
        const auto response = api.handleRequest({ "POST", "/simulate", "{{{", {} });
        CHECK(response.status == 400);
        CHECK(bodyAsJson(response)["status"] == 400);
    }

    SUBCASE("bad side")
    {
        const auto response = api.handleRequest({ "POST", "/simulate",
            R"({"side":"up","mode":"notional","size":10})", {} });
        CHECK(response.status == 400);
    }

    SUBCASE("negative size rejected")
    {
        const auto response = api.handleRequest({ "POST", "/simulate",
            R"({"side":"buy","mode":"notional","size":-5})", {} });
        CHECK(response.status == 400);
    }
}

TEST_CASE("live mode with unavailable book refuses simulation honestly")
{
    qx::server::ApiServer api;
    api.describeEngine("live", "okx", "BTC-USDT", "books");

    qx::server::ApiServer::EngineView view;
    view.health.state = qx::feed::ConnectionState::Connecting;
    view.health.bookReady = false;
    api.updateEngineState(view);

    const auto health = bodyAsJson(api.handleRequest({ "GET", "/health", "", {} }));
    CHECK(health["mode"] == "live");
    CHECK(health["exchange"] == "okx");
    CHECK(health["instrument"] == "BTC-USDT");
    CHECK(health["channel"] == "books");
    CHECK(health["connected"] == false);
    CHECK(health["bookReady"] == false);

    // No silent fallback to fixture data: simulation is refused outright.
    const auto sim = api.handleRequest({ "POST", "/simulate",
        R"({"side":"buy","mode":"notional","size":25000,"feeBps":5})", {} });
    CHECK(sim.status == 503);
}

TEST_CASE("gap invalidation is visible through /health")
{
    qx::server::ApiServer api;
    primeReplayBook(api);

    qx::server::ApiServer::EngineView invalidated;
    invalidated.bids = {};
    invalidated.asks = {};
    invalidated.lastSequence = 0;
    invalidated.health.state = qx::feed::ConnectionState::Connected;
    invalidated.health.sequenceGaps = 1;
    invalidated.health.bookReady = false;
    api.updateEngineState(invalidated);

    const auto health = bodyAsJson(api.handleRequest({ "GET", "/health", "", {} }));
    CHECK(health["sequenceGaps"] == 1);
    CHECK(health["bookReady"] == false);

    const auto book = bodyAsJson(api.handleRequest({ "GET", "/book", "", {} }));
    CHECK(book["bids"].empty());
}