#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdint>
#include <string>

#include "qx/Decoder.h"

namespace {

std::string okxSnapshot()
{
    return R"({
        "arg": {"channel": "books", "instId": "BTC-USDT"},
        "action": "snapshot",
        "data": [{
            "asks": [["63001.0", "0.90", "0", "1"], ["63002.5", "1.50", "0", "2"]],
            "bids": [["63000.5", "1.20", "0", "1"], ["62999.0", "0.75", "0", "3"]],
            "ts": "1755850000000",
            "prevSeqId": -1,
            "seqId": 10,
            "checksum": -180370240
        }]
    })";
}

std::string okxUpdate(std::uint64_t seqId, std::int64_t prevSeqId)
{
    return R"({"action":"update","data":[{"seqId":)" + std::to_string(seqId)
        + R"(,"prevSeqId":)" + std::to_string(prevSeqId)
        + R"(,"bids":[["100.0","1.0"]],"asks":[],"ts":"1755850001000"}]})";
}

} // namespace

TEST_CASE("real OKX books snapshot decodes with seqId/prevSeqId semantics")
{
    const auto frame = qx::decodeFrame(okxSnapshot(), qx::FeedFormat::OkxBooks);

    REQUIRE(frame.status == qx::DecodeStatus::Ok);
    CHECK(frame.event.type == qx::EventType::Snapshot);
    CHECK(frame.event.sequence == 10u);
    REQUIRE(frame.event.prevSequence.has_value());
    CHECK(*frame.event.prevSequence == -1);
    CHECK(frame.event.hasSequence);
    // OKX ts is milliseconds -> engine stores nanoseconds.
    CHECK(frame.event.timestampNs == 1755850000000LL * 1000000LL);
    REQUIRE(frame.event.bids.size() == 2);
    REQUIRE(frame.event.asks.size() == 2);
    CHECK(frame.event.bids[0].price == doctest::Approx(63000.5));
    CHECK(frame.event.bids[0].size == doctest::Approx(1.20));
    CHECK(frame.event.asks[1].price == doctest::Approx(63002.5));
    CHECK(frame.event.checksum.has_value());
    CHECK(*frame.event.checksum == -180370240);
}

TEST_CASE("OKX incremental update keeps raw seqId pair without +1 assumptions")
{
    const auto frame = qx::decodeFrame(okxUpdate(15, 10), qx::FeedFormat::OkxBooks);

    REQUIRE(frame.status == qx::DecodeStatus::Ok);
    CHECK(frame.event.type == qx::EventType::Delta);
    CHECK(frame.event.sequence == 15u);
    REQUIRE(frame.event.prevSequence.has_value());
    CHECK(*frame.event.prevSequence == 10);
}

TEST_CASE("no-change keepalive frame (empty sides, same seq) decodes cleanly")
{
    const std::string payload = R"({"action":"update","data":[{
        "seqId":19,"prevSeqId":19,"bids":[],"asks":[],"ts":"1755850002000"}]})";

    const auto frame = qx::decodeFrame(payload, qx::FeedFormat::OkxBooks);

    REQUIRE(frame.status == qx::DecodeStatus::Ok);
    CHECK(frame.event.sequence == 19u);
    CHECK(*frame.event.prevSequence == 19);
    CHECK(frame.event.bids.empty());
    CHECK(frame.event.asks.empty());
}

TEST_CASE("maintenance reset frames carry a lower seqId and decode fine")
{
    const std::string payload = R"({"action":"update","data":[{
        "seqId":3,"prevSeqId":25,"bids":[["50.0","1"]],"asks":[]}]})";

    const auto frame = qx::decodeFrame(payload, qx::FeedFormat::OkxBooks);

    REQUIRE(frame.status == qx::DecodeStatus::Ok);
    CHECK(frame.event.sequence == 3u);
    CHECK(*frame.event.prevSequence == 25);
}

TEST_CASE("seqId fields accept numeric strings too")
{
    const std::string payload = R"({"action":"update","data":[{
        "seqId":"1234","prevSeqId":"1230","bids":[],"asks":[]}]})";

    const auto frame = qx::decodeFrame(payload, qx::FeedFormat::OkxBooks);
    REQUIRE(frame.status == qx::DecodeStatus::Ok);
    CHECK(frame.event.sequence == 1234u);
    CHECK(*frame.event.prevSequence == 1230);
}

TEST_CASE("strict level parsing at the decoder boundary")
{
    SUBCASE("zero size is a valid delete")
    {
        const std::string payload = R"({"action":"update","data":[{
            "seqId":2,"prevSeqId":1,"bids":[["100.5","0"]],"asks":[]}]})";
        const auto frame = qx::decodeFrame(payload, qx::FeedFormat::OkxBooks);
        REQUIRE(frame.status == qx::DecodeStatus::Ok);
        REQUIRE(frame.event.bids.size() == 1);
        CHECK(frame.event.bids[0].price == doctest::Approx(100.5));
        CHECK(frame.event.bids[0].size == 0.0);
    }

    SUBCASE("negative price rejected")
    {
        const std::string payload = R"({"action":"update","data":[{
            "seqId":2,"prevSeqId":1,"bids":[["-100.5","1"]],"asks":[]}]})";
        CHECK(qx::decodeFrame(payload, qx::FeedFormat::OkxBooks).status
            == qx::DecodeStatus::Malformed);
    }

    SUBCASE("zero price rejected")
    {
        const std::string payload = R"({"action":"update","data":[{
            "seqId":2,"prevSeqId":1,"bids":[["0","1"]],"asks":[]}]})";
        CHECK(qx::decodeFrame(payload, qx::FeedFormat::OkxBooks).status
            == qx::DecodeStatus::Malformed);
    }

    SUBCASE("negative size rejected")
    {
        const std::string payload = R"({"action":"update","data":[{
            "seqId":2,"prevSeqId":1,"bids":[["100.5","-1"]],"asks":[]}]})";
        CHECK(qx::decodeFrame(payload, qx::FeedFormat::OkxBooks).status
            == qx::DecodeStatus::Malformed);
    }

    SUBCASE("empty values rejected")
    {
        const std::string payload = R"({"action":"update","data":[{
            "seqId":2,"prevSeqId":1,"bids":[["","1"]],"asks":[]}]})";
        CHECK(qx::decodeFrame(payload, qx::FeedFormat::OkxBooks).status
            == qx::DecodeStatus::Malformed);
    }

    SUBCASE("trailing junk rejected")
    {
        const std::string payload = R"({"action":"update","data":[{
            "seqId":2,"prevSeqId":1,"bids":[["100.2abc","1"]],"asks":[]}]})";
        CHECK(qx::decodeFrame(payload, qx::FeedFormat::OkxBooks).status
            == qx::DecodeStatus::Malformed);
    }

    SUBCASE("+Inf/-Inf/NaN rejected")
    {
        for (const char* bad : { "inf", "-inf", "nan", "Infinity", "NaN" }) {
            const std::string payload = std::string(R"({"action":"update","data":[{
                "seqId":2,"prevSeqId":1,"bids":[[")") + bad + R"(","1"]],"asks":[]}]})";
            CAPTURE(bad);
            CHECK(qx::decodeFrame(payload, qx::FeedFormat::OkxBooks).status
                == qx::DecodeStatus::Malformed);
        }
    }

    SUBCASE("overflowing exponent rejected")
    {
        const std::string payload = R"({"action":"update","data":[{
            "seqId":2,"prevSeqId":1,"bids":[["1e999","1"]],"asks":[]}]})";
        CHECK(qx::decodeFrame(payload, qx::FeedFormat::OkxBooks).status
            == qx::DecodeStatus::Malformed);
    }

    SUBCASE("leading plus sign rejected")
    {
        const std::string payload = R"({"action":"update","data":[{
            "seqId":2,"prevSeqId":1,"bids":[["+100.5","1"]],"asks":[]}]})";
        CHECK(qx::decodeFrame(payload, qx::FeedFormat::OkxBooks).status
            == qx::DecodeStatus::Malformed);
    }
}

TEST_CASE("sequence metadata is mandatory on OKX books data")
{
    SUBCASE("missing seqId")
    {
        const std::string payload = R"({"action":"update","data":[{
            "prevSeqId":10,"bids":[],"asks":[]}]})";
        CHECK(qx::decodeFrame(payload, qx::FeedFormat::OkxBooks).status
            == qx::DecodeStatus::Malformed);
    }

    SUBCASE("missing prevSeqId")
    {
        const std::string payload = R"({"action":"update","data":[{
            "seqId":11,"bids":[],"asks":[]}]})";
        CHECK(qx::decodeFrame(payload, qx::FeedFormat::OkxBooks).status
            == qx::DecodeStatus::Malformed);
    }

    SUBCASE("prevSeqId below -1 is impossible")
    {
        const std::string payload = R"({"action":"update","data":[{
            "seqId":11,"prevSeqId":-2,"bids":[],"asks":[]}]})";
        CHECK(qx::decodeFrame(payload, qx::FeedFormat::OkxBooks).status
            == qx::DecodeStatus::Malformed);
    }

    SUBCASE("negative seqId is impossible")
    {
        const std::string payload = R"({"action":"snapshot","data":[{
            "seqId":-5,"prevSeqId":-1,"bids":[],"asks":[]}]})";
        CHECK(qx::decodeFrame(payload, qx::FeedFormat::OkxBooks).status
            == qx::DecodeStatus::Malformed);
    }

    SUBCASE("fractional seqId is impossible")
    {
        const std::string payload = R"({"action":"update","data":[{
            "seqId":11.5,"prevSeqId":10,"bids":[],"asks":[]}]})";
        CHECK(qx::decodeFrame(payload, qx::FeedFormat::OkxBooks).status
            == qx::DecodeStatus::Malformed);
    }
}

TEST_CASE("timestamps are milliseconds converted to nanoseconds with overflow checks")
{
    SUBCASE("numeric ts converts")
    {
        const std::string payload = R"({"action":"update","data":[{
            "seqId":2,"prevSeqId":1,"bids":[],"asks":[],"ts":1755850003000}]})";
        const auto frame = qx::decodeFrame(payload, qx::FeedFormat::OkxBooks);
        REQUIRE(frame.status == qx::DecodeStatus::Ok);
        CHECK(frame.event.timestampNs == 1755850003000LL * 1000000LL);
    }

    SUBCASE("garbage ts string rejected")
    {
        const std::string payload = R"({"action":"update","data":[{
            "seqId":2,"prevSeqId":1,"bids":[],"asks":[],"ts":"12xab"}]})";
        CHECK(qx::decodeFrame(payload, qx::FeedFormat::OkxBooks).status
            == qx::DecodeStatus::Malformed);
    }

    SUBCASE("overflowing ts rejected instead of silently wrapping")
    {
        // One ms above INT64_MAX/1e6: multiplying would overflow int64 ns.
        const std::string payload = R"({"action":"update","data":[{
            "seqId":2,"prevSeqId":1,"bids":[],"asks":[],"ts":9223372036854}]})";
        CHECK(qx::decodeFrame(payload, qx::FeedFormat::OkxBooks).status
            == qx::DecodeStatus::Malformed);
    }
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

    SUBCASE("unknown action")
    {
        const auto frame = qx::decodeFrame(
            R"({"action": "pong", "data": []})", qx::FeedFormat::OkxBooks);
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
            R"({"action": "snapshot", "data": [{"seqId":1,"prevSeqId":-1,"bids": [[1, 2]], "asks": []}]})",
            qx::FeedFormat::OkxBooks);
        CHECK(frame.status == qx::DecodeStatus::Malformed);
    }

    SUBCASE("non-numeric price strings")
    {
        const auto frame = qx::decodeFrame(
            R"({"action": "snapshot", "data": [{"seqId":1,"prevSeqId":-1,"bids": [["abc", "1"]], "asks": []}]})",
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
    CHECK_FALSE(frame.event.hasSequence);
    CHECK(frame.event.sequence == 0u);
    CHECK_FALSE(frame.event.prevSequence.has_value());
    REQUIRE(frame.event.bids.size() == 2);
    REQUIRE(frame.event.asks.size() == 2);
}

TEST_CASE("proxy-mode strict parsing still applies to levels")
{
    const std::string payload = R"({
        "asks": [["63001.0", "0.90"]],
        "bids": [["63000.5", "trailing-junk"]]
    })";

    CHECK(qx::decodeFrame(payload, qx::FeedFormat::ProxyBooks).status
        == qx::DecodeStatus::Malformed);
}