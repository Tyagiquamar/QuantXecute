#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "qx/Decoder.h"

namespace {

std::string okxSnapshot()
{
    return R"({
        "arg": {"channel": "books", "instId": "BTC-USDT-SWAP"},
        "action": "snapshot",
        "data": [{
            "asks": [["63001.0", "0.90", "0", "1"], ["63002.5", "1.50", "0", "2"]],
            "bids": [["63000.5", "1.20", "0", "1"], ["62999.0", "0.75", "0", "3"]],
            "ts": "1755850000000000000",
            "checksum": -180370240
        }]
    })";
}

} // namespace

TEST_CASE("well-formed OKX snapshot decodes to typed event")
{
    const auto frame = qx::decodeFrame(okxSnapshot(), qx::FeedFormat::OkxBooks);

    REQUIRE(frame.status == qx::DecodeStatus::Ok);
    CHECK(frame.event.type == qx::EventType::Snapshot);
    REQUIRE(frame.event.bids.size() == 2);
    REQUIRE(frame.event.asks.size() == 2);
    CHECK(frame.event.bids[0].price == doctest::Approx(63000.5));
    CHECK(frame.event.bids[0].size == doctest::Approx(1.20));
    CHECK(frame.event.asks[1].price == doctest::Approx(63002.5));
    CHECK(frame.event.checksum.has_value());
    CHECK(*frame.event.checksum == -180370240);
}

TEST_CASE("OKX update frame decodes as delta")
{
    const std::string payload = R"({
        "arg": {"channel": "books"},
        "action": "update",
        "data": [{
            "asks": [["63003.0", "0"]],
            "bids": [],
            "ts": 1755850001000000000
        }]
    })";

    const auto frame = qx::decodeFrame(payload, qx::FeedFormat::OkxBooks);

    REQUIRE(frame.status == qx::DecodeStatus::Ok);
    CHECK(frame.event.type == qx::EventType::Delta);
    REQUIRE(frame.event.asks.size() == 1);
    CHECK(frame.event.asks[0].price == doctest::Approx(63003.0));
    CHECK(frame.event.asks[0].size == 0.0);
    CHECK(frame.event.bids.empty());
    CHECK_FALSE(frame.event.checksum.has_value());
}

TEST_CASE("sequence field is extracted when present")
{
    const std::string payload = R"({
        "action": "update",
        "data": [{"seq": 42, "bids": [["1.0", "1"]], "asks": []}]
    })";

    const auto frame = qx::decodeFrame(payload, qx::FeedFormat::OkxBooks);

    REQUIRE(frame.status == qx::DecodeStatus::Ok);
    CHECK(frame.event.sequence == 42);
}

TEST_CASE("malformed frames report error without throwing")
{
    SUBCASE("garbage text")
    {
        const auto frame = qx::decodeFrame("not json at all {", qx::FeedFormat::OkxBooks);
        CHECK(frame.status == qx::DecodeStatus::Malformed);
    }

    SUBCASE("valid json, wrong shape")
    {
        const auto frame = qx::decodeFrame(R"([1,2,3])", qx::FeedFormat::OkxBooks);
        CHECK(frame.status == qx::DecodeStatus::Malformed);
    }

    SUBCASE("missing action")
    {
        const auto frame = qx::decodeFrame(R"({"data": []})", qx::FeedFormat::OkxBooks);
        CHECK(frame.status == qx::DecodeStatus::Malformed);
    }

    SUBCASE("empty data array")
    {
        const auto frame = qx::decodeFrame(
            R"({"action": "snapshot", "data": []})", qx::FeedFormat::OkxBooks);
        CHECK(frame.status == qx::DecodeStatus::Malformed);
    }

    SUBCASE("levels not string pairs")
    {
        const auto frame = qx::decodeFrame(
            R"({"action": "snapshot", "data": [{"bids": [[1, 2]], "asks": []}]})",
            qx::FeedFormat::OkxBooks);
        CHECK(frame.status == qx::DecodeStatus::Malformed);
    }

    SUBCASE("non-numeric price strings")
    {
        const auto frame = qx::decodeFrame(
            R"({"action": "snapshot", "data": [{"bids": [["abc", "1"]], "asks": []}]})",
            qx::FeedFormat::OkxBooks);
        CHECK(frame.status == qx::DecodeStatus::Malformed);
    }
}

TEST_CASE("proxy-mode frame without sequence decodes cleanly")
{
    const std::string payload = R"({
        "asks": [["63001.0", "0.90"], ["63002.5", "1.50"]],
        "bids": [["63000.5", "1.20"], ["62999.0", "0.75"]],
        "timestamp_ns": 1755850000000000000
    })";

    const auto frame = qx::decodeFrame(payload, qx::FeedFormat::ProxyBooks);

    REQUIRE(frame.status == qx::DecodeStatus::Ok);
    CHECK(frame.event.type == qx::EventType::Snapshot);
    CHECK(frame.event.sequence == 0);
    REQUIRE(frame.event.bids.size() == 2);
    REQUIRE(frame.event.asks.size() == 2);
}
