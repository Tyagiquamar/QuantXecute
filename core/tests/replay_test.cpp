#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "fixture_loader.h"
#include "qx/Recorder.h"

namespace {

std::string tempLogPath(const std::string& name)
{
    return (std::filesystem::temp_directory_path() / name).string();
}

std::vector<qx::MarketEvent> readAll(qx::EventLogReader& reader)
{
    std::vector<qx::MarketEvent> events;
    while (auto event = reader.next()) {
        events.push_back(*event);
    }
    return events;
}

} // namespace

TEST_CASE("recorder round-trips events identically")
{
    const auto original = qx::test::loadEventsJsonl(
        std::string(QX_FIXTURE_DIR) + "/btc_snapshot_deltas.jsonl");
    REQUIRE(original.size() == 4);

    const auto path = tempLogPath("qx_recorder_roundtrip.jsonl");
    { std::error_code ec; std::filesystem::remove(path, ec); }

    {
        qx::Recorder recorder(path);
        REQUIRE(recorder.isOpen());
        for (const auto& event : original) {
            CHECK(recorder.record(event));
        }
    }

    qx::EventLogReader reader(path);
    const auto restored = readAll(reader);

    REQUIRE(restored.size() == original.size());
    for (std::size_t i = 0; i < original.size(); ++i) {
        CHECK(restored[i] == original[i]);
    }
    CHECK(reader.malformedLines() == 0);
    CHECK(reader.eof());

    { std::error_code ec; std::filesystem::remove(path, ec); }
}

TEST_CASE("large timestamps and negative checksums round-trip exactly")
{
    const auto path = tempLogPath("qx_recorder_precision.jsonl");
    { std::error_code ec; std::filesystem::remove(path, ec); }

    qx::MarketEvent event;
    event.type = qx::EventType::Snapshot;
    // seqId domain is the signed int64 range; UINT64_MAX is not a legal id.
    event.sequence = 9223372036854775807ull;
    event.timestampNs = 1755850000123456789LL;
    event.checksum = -180370240;
    event.bids = { { 63000.5, 1.20 } };
    event.asks = {};

    {
        qx::Recorder recorder(path);
        CHECK(recorder.record(event));
    }

    qx::EventLogReader reader(path);
    const auto restored = readAll(reader);

    REQUIRE(restored.size() == 1);
    CHECK(restored[0] == event);

    { std::error_code ec; std::filesystem::remove(path, ec); }
}

TEST_CASE("truncated final line is skipped and reported, not fatal")
{
    const auto path = tempLogPath("qx_recorder_truncated.jsonl");
    {
        std::ofstream out(path);
        out << R"({"type":"snapshot","sequence":1,"timestamp_ns":10,"bids":[{"price":"100","size":"1"}],"asks":[]})" << '\n';
        out << R"({"type":"delta","sequence":2,"timestamp_ns":20,"bids":[{"price":"101","size":"2"}],"asks":[]})" << '\n';
        out << R"({"type":"delta","sequence":3,"timestamp_)";
    }

    qx::EventLogReader reader(path);
    const auto restored = readAll(reader);

    REQUIRE(restored.size() == 2);
    CHECK(restored[0].sequence == 1);
    CHECK(restored[1].sequence == 2);
    CHECK(reader.malformedLines() == 1);
    CHECK(reader.eof());

    { std::error_code ec; std::filesystem::remove(path, ec); }
}

TEST_CASE("level values obey the strict whole-string law at the log boundary")
{
    const auto path = tempLogPath("qx_recorder_strict_levels.jsonl");
    {
        std::ofstream out(path);
        // trailing junk, NaN, negative size, negative price: all malformed
        out << R"({"type":"snapshot","sequence":1,"timestamp_ns":10,"bids":[{"price":"100.2abc","size":"1"}],"asks":[]})" << '\n';
        out << R"({"type":"snapshot","sequence":1,"timestamp_ns":10,"bids":[],"asks":[{"price":"nan","size":"1"}]})" << '\n';
        out << R"({"type":"snapshot","sequence":1,"timestamp_ns":10,"bids":[{"price":"100","size":"-1"}],"asks":[]})" << '\n';
        out << R"({"type":"snapshot","sequence":1,"timestamp_ns":10,"bids":[{"price":"-100","size":"1"}],"asks":[]})" << '\n';
        // a well-formed line still parses after the malformed ones
        out << R"({"type":"snapshot","sequence":1,"timestamp_ns":10,"bids":[{"price":"100.5","size":"2.5"}],"asks":[]})" << '\n';
    }

    qx::EventLogReader reader(path);
    const auto restored = readAll(reader);

    REQUIRE(restored.size() == 1);
    CHECK(restored[0].bids.size() == 1);
    CHECK(reader.malformedLines() == 4);

    { std::error_code ec; std::filesystem::remove(path, ec); }
}

TEST_CASE("missing file yields empty stream without crash")
{
    qx::EventLogReader reader(tempLogPath("qx_does_not_exist_12345.jsonl"));
    CHECK_FALSE(reader.next().has_value());
    CHECK(reader.eof());
}
