#include "qx/Decoder.h"

#include <nlohmann/json.hpp>

namespace qx {

namespace {

bool parseLevels(const nlohmann::json& arr, std::vector<Level>& out)
{
    if (!arr.is_array()) {
        return false;
    }
    out.reserve(out.size() + arr.size());
    for (const auto& entry : arr) {
        if (!entry.is_array() || entry.size() < 2 || !entry[0].is_string() || !entry[1].is_string()) {
            return false;
        }
        const auto price = std::stod(entry[0].get<std::string>());
        const auto size = std::stod(entry[1].get<std::string>());
        out.push_back(Level { price, size });
    }
    return true;
}

std::optional<std::int64_t> parseTimestampMs(const nlohmann::json& j)
{
    if (auto it = j.find("ts"); it != j.end()) {
        try {
            if (it->is_string()) {
                return std::stoll(it->get<std::string>()) * 1'000'000LL;
            }
            if (it->is_number_integer()) {
                return it->get<std::int64_t>() * 1'000'000LL;
            }
        } catch (...) {
        }
    }
    if (auto it = j.find("timestamp_ns"); it != j.end() && it->is_number_integer()) {
        return it->get<std::int64_t>();
    }
    return std::nullopt;
}

std::optional<std::uint64_t> parseSequence(const nlohmann::json& j)
{
    for (const char* key : { "seq", "sequence" }) {
        if (auto it = j.find(key); it != j.end() && it->is_number_unsigned()) {
            return it->get<std::uint64_t>();
        }
    }
    return std::nullopt;
}

DecodedFrame decodeOkxFrame(const nlohmann::json& j)
{
    DecodedFrame frame;

    const auto actionIt = j.find("action");
    const auto dataIt = j.find("data");
    if (actionIt == j.end() || dataIt == j.end() || !actionIt->is_string()
        || !dataIt->is_array() || dataIt->empty()) {
        return DecodedFrame { DecodeStatus::Malformed, {} };
    }

    const auto action = actionIt->get<std::string>();
    if (action != "snapshot" && action != "update") {
        return DecodedFrame { DecodeStatus::Malformed, {} };
    }

    MarketEvent event;
    event.type = action == "snapshot" ? EventType::Snapshot : EventType::Delta;

    const auto& entry = dataIt->front();
    if (!entry.is_object()) {
        return DecodedFrame { DecodeStatus::Malformed, {} };
    }

    const auto asksIt = entry.find("asks");
    const auto bidsIt = entry.find("bids");
    if (asksIt == entry.end() || bidsIt == entry.end()) {
        return DecodedFrame { DecodeStatus::Malformed, {} };
    }

    if (!parseLevels(*asksIt, event.asks) || !parseLevels(*bidsIt, event.bids)) {
        return DecodedFrame { DecodeStatus::Malformed, {} };
    }

    if (const auto ts = parseTimestampMs(entry)) {
        event.timestampNs = *ts;
    }
    if (const auto seq = parseSequence(entry)) {
        event.sequence = *seq;
    }
    if (auto it = entry.find("checksum"); it != entry.end() && it->is_number_integer()) {
        event.checksum = it->get<std::int64_t>();
    }

    return DecodedFrame { DecodeStatus::Ok, std::move(event) };
}

DecodedFrame decodeProxyFrame(const nlohmann::json& j)
{
    DecodedFrame frame;

    const auto asksIt = j.find("asks");
    const auto bidsIt = j.find("bids");
    if (asksIt == j.end() || bidsIt == j.end()) {
        return DecodedFrame { DecodeStatus::Malformed, {} };
    }

    MarketEvent event;

    if (const auto actionIt = j.find("action"); actionIt != j.end() && actionIt->is_string()) {
        const auto action = actionIt->get<std::string>();
        if (action != "snapshot" && action != "update") {
            return DecodedFrame { DecodeStatus::Malformed, {} };
        }
        event.type = action == "snapshot" ? EventType::Snapshot : EventType::Delta;
    } else {
        event.type = EventType::Snapshot;
    }

    if (!parseLevels(*asksIt, event.asks) || !parseLevels(*bidsIt, event.bids)) {
        return DecodedFrame { DecodeStatus::Malformed, {} };
    }

    if (const auto ts = parseTimestampMs(j)) {
        event.timestampNs = *ts;
    }
    if (const auto seq = parseSequence(j)) {
        event.sequence = *seq;
    } else {
        event.sequence = 0;
    }

    return DecodedFrame { DecodeStatus::Ok, std::move(event) };
}

} // namespace

DecodedFrame decodeFrame(std::string_view payload, FeedFormat format)
{
    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(payload);
    } catch (...) {
        return DecodedFrame { DecodeStatus::Malformed, {} };
    }

    if (!parsed.is_object()) {
        return DecodedFrame { DecodeStatus::Malformed, {} };
    }

    try {
        switch (format) {
        case FeedFormat::OkxBooks:
            return decodeOkxFrame(parsed);
        case FeedFormat::ProxyBooks:
            return decodeProxyFrame(parsed);
        }
    } catch (...) {
        return DecodedFrame { DecodeStatus::Malformed, {} };
    }

    return DecodedFrame { DecodeStatus::Malformed, {} };
}

}
