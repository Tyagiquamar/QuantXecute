#include "qx/Recorder.h"

#include <charconv>
#include <chrono>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace qx {

namespace {

template <typename T>
void appendInt(std::string& out, T value)
{
    char buffer[24];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    out.append(buffer, static_cast<std::size_t>(result.ptr - buffer));
}

void appendDouble(std::string& out, double value)
{
    char buffer[32];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    out.append(buffer, static_cast<std::size_t>(result.ptr - buffer));
}

std::string levelToJson(const Level& level)
{
    std::string out = "{\"price\":\"";
    appendDouble(out, level.price);
    out += "\",\"size\":\"";
    appendDouble(out, level.size);
    out += "\"}";
    return out;
}

std::string levelsToJson(const std::vector<Level>& levels)
{
    if (levels.empty()) {
        return "[]";
    }
    std::string out = "[";
    for (std::size_t i = 0; i < levels.size(); ++i) {
        if (i != 0) {
            out += ",";
        }
        out += levelToJson(levels[i]);
    }
    out += "]";
    return out;
}

} // namespace

Recorder::Recorder(const std::string& path)
    : out_(path, std::ios::app)
{
}

Recorder::~Recorder()
{
    if (out_.is_open()) {
        out_.flush();
    }
}

bool Recorder::isOpen() const
{
    return out_.is_open();
}

bool Recorder::record(const MarketEvent& event)
{
    if (!out_.is_open()) {
        return false;
    }

    std::string line;
    line.reserve(128 + 64 * (event.bids.size() + event.asks.size()));

    line += "{\"type\":\"";
    line += event.type == EventType::Snapshot ? "snapshot" : "delta";
    line += "\",\"seqId\":";
    appendInt(line, event.sequence);

    // Persist the exact feed sequencing metadata so replay reproduces the
    // same validation decisions. prevSeqId keeps its signed -1 snapshot form.
    if (event.prevSequence.has_value()) {
        line += ",\"prevSeqId\":";
        appendInt(line, *event.prevSequence);
    }
    if (!event.hasSequence) {
        line += ",\"sequenced\":false";
    }

    line += ",\"timestamp_ns\":";
    appendInt(line, event.timestampNs);
    line += ",\"captured_at_ns\":";
    appendInt(line, std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count());
    line += ",\"bids\":";
    line += levelsToJson(event.bids);
    line += ",\"asks\":";
    line += levelsToJson(event.asks);

    if (event.checksum.has_value()) {
        line += ",\"checksum\":";
        appendInt(line, *event.checksum);
    }

    line += "}\n";

    out_ << line;
    out_.flush();

    return static_cast<bool>(out_);
}

EventLogReader::EventLogReader(const std::string& path)
    : in_(path)
{
}

std::optional<MarketEvent> EventLogReader::next()
{
    if (!in_.is_open()) {
        eof_ = true;
        return std::nullopt;
    }

    std::string line;
    while (std::getline(in_, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }

        try {
            const nlohmann::json parsed = nlohmann::json::parse(line);

            MarketEvent event;
            const auto typeIt = parsed.find("type");
            if (typeIt == parsed.end() || !typeIt->is_string()) {
                ++malformedLines_;
                continue;
            }
            event.type = typeIt->get<std::string>() == "snapshot" ? EventType::Snapshot : EventType::Delta;

            // Sequenced logs carry seqId/prevSeqId. Legacy logs with only a
            // dense "sequence" field are still readable, but their events
            // lack prevSeqId metadata and therefore cannot pass strict
            // continuity validation.
            bool sequenced = true;
            if (auto it = parsed.find("sequenced"); it != parsed.end()
                && it->is_boolean()) {
                sequenced = it->get<bool>();
            }

            // The raw id is preserved even for unsequenced logs so recorded
            // events round-trip byte-identically; only the hasSequence flag
            // reflects whether the transport guarantees sequencing.
            bool haveSeq = false;
            if (auto it = parsed.find("seqId"); it != parsed.end()
                && it->is_number_unsigned()) {
                event.sequence = it->get<std::uint64_t>();
                haveSeq = true;
            } else if (auto legacy = parsed.find("sequence");
                       legacy != parsed.end() && legacy->is_number_unsigned()) {
                event.sequence = legacy->get<std::uint64_t>();
                haveSeq = true;
            }

            event.hasSequence = haveSeq && sequenced;
            if (!haveSeq) {
                event.sequence = 0;
            } else if (sequenced) {
                if (auto it = parsed.find("prevSeqId");
                    it != parsed.end() && it->is_number_integer()) {
                    event.prevSequence = it->get<std::int64_t>();
                }
            }

            event.timestampNs = parsed.value("timestamp_ns", static_cast<std::int64_t>(0));

            for (const char* side : { "bids", "asks" }) {
                auto& levels = side[0] == 'b' ? event.bids : event.asks;
                const auto sideIt = parsed.find(side);
                if (sideIt == parsed.end() || !sideIt->is_array()) {
                    throw std::runtime_error("missing side");
                }
                for (const auto& entry : *sideIt) {
                    // Prices/sizes are stored as strings (matching the
                    // writer and exchange convention); parse them strictly.
                    const auto price = std::stod(entry.at("price").get<std::string>());
                    const auto size = std::stod(entry.at("size").get<std::string>());
                    levels.push_back(Level { price, size });
                }
            }

            if (auto it = parsed.find("checksum"); it != parsed.end() && it->is_number_integer()) {
                event.checksum = it->get<std::int64_t>();
            }

            return event;
        } catch (...) {
            ++malformedLines_;
            continue;
        }
    }

    eof_ = true;
    return std::nullopt;
}

}