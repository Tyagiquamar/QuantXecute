#include "qx/Decoder.h"

#include <charconv>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <system_error>

#include <nlohmann/json.hpp>

namespace qx {

namespace {

// Strict floating-point parse: the entire string must be consumed and the
// value must be finite. Rejects "", "+1", "1.0abc", "nan", "inf" and overflow.
std::optional<double> parseStrictDouble(std::string_view text)
{
    if (text.empty()) {
        return std::nullopt;
    }

    double value = 0.0;
    const char* first = text.data();
    const char* last = text.data() + text.size();
    const auto result = std::from_chars(first, last, value);
    if (result.ec != std::errc {} || result.ptr != last) {
        return std::nullopt;
    }
    if (!std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

// Strict signed integer parse with the same whole-string rule.
std::optional<std::int64_t> parseStrictInt64(std::string_view text)
{
    if (text.empty()) {
        return std::nullopt;
    }

    std::int64_t value = 0;
    const char* first = text.data();
    const char* last = text.data() + text.size();
    const auto result = std::from_chars(first, last, value);
    if (result.ec != std::errc {} || result.ptr != last) {
        return std::nullopt;
    }
    return value;
}

enum class FieldPresence : std::uint8_t {
    Absent,
    Ok,
    Invalid,
};

struct IntField {
    FieldPresence presence = FieldPresence::Absent;
    std::int64_t value = 0;
};

// Reads an OKX integer field that may arrive as a JSON number or a numeric
// string. min/max bound the legal domain (prevSeqId allows -1; seqId does not).
IntField parseIntField(const nlohmann::json& parent, const char* key,
    std::int64_t minValue, std::int64_t maxValue)
{
    IntField field;

    const auto it = parent.find(key);
    if (it == parent.end() || it->is_null()) {
        return field;
    }

    if (it->is_boolean() || it->is_number_float() || it->is_array() || it->is_object()) {
        field.presence = FieldPresence::Invalid;
        return field;
    }

    if (it->is_string()) {
        const auto parsed = parseStrictInt64(it->get<std::string>());
        if (!parsed.has_value() || *parsed < minValue || *parsed > maxValue) {
            field.presence = FieldPresence::Invalid;
            return field;
        }
        field.presence = FieldPresence::Ok;
        field.value = *parsed;
        return field;
    }

    // JSON integers only from here: nlohmann stores unsigned and signed
    // separately, so both must be range-checked into int64 explicitly.
    if (it->is_number_unsigned()) {
        const auto raw = it->get<std::uint64_t>();
        if (raw > static_cast<std::uint64_t>(maxValue)) {
            field.presence = FieldPresence::Invalid;
            return field;
        }
        field.presence = FieldPresence::Ok;
        field.value = static_cast<std::int64_t>(raw);
        return field;
    }

    const auto raw = it->get<std::int64_t>();
    if (raw < minValue || raw > maxValue) {
        field.presence = FieldPresence::Invalid;
        return field;
    }
    field.presence = FieldPresence::Ok;
    field.value = raw;
    return field;
}

// price must parse fully, be finite and strictly positive.
// size must parse fully, be finite and non-negative (size 0 is a delete).
bool parseLevels(const nlohmann::json& arr, std::vector<Level>& out)
{
    if (!arr.is_array()) {
        return false;
    }
    out.reserve(out.size() + arr.size());
    for (const auto& entry : arr) {
        if (!entry.is_array() || entry.size() < 2 || !entry[0].is_string()
            || !entry[1].is_string()) {
            return false;
        }
        const auto price = parseStrictDouble(entry[0].get<std::string>());
        if (!price.has_value() || *price <= 0.0) {
            return false;
        }
        const auto size = parseStrictDouble(entry[1].get<std::string>());
        if (!size.has_value() || *size < 0.0) {
            return false;
        }
        out.push_back(Level { *price, *size });
    }
    return true;
}

// OKX `ts` is milliseconds since epoch as a string or number. Convert to
// nanoseconds with an explicit overflow guard instead of an unchecked
// multiplication that can silently wrap int64.
FieldPresence parseTimestampMsToNs(const nlohmann::json& parent, std::int64_t& outNs)
{
    constexpr std::int64_t kNsPerMs = 1000000LL;
    constexpr std::int64_t kMaxSafeMs = std::numeric_limits<std::int64_t>::max() / kNsPerMs;

    const auto it = parent.find("ts");
    if (it == parent.end() || it->is_null()) {
        return FieldPresence::Absent;
    }

    std::int64_t ms = 0;
    if (it->is_string()) {
        const auto parsed = parseStrictInt64(it->get<std::string>());
        if (!parsed.has_value()) {
            return FieldPresence::Invalid;
        }
        ms = *parsed;
    } else if (it->is_number_unsigned()) {
        const auto raw = it->get<std::uint64_t>();
        if (raw > static_cast<std::uint64_t>(kMaxSafeMs)) {
            return FieldPresence::Invalid;
        }
        ms = static_cast<std::int64_t>(raw);
    } else if (it->is_number_integer()) {
        ms = it->get<std::int64_t>();
    } else {
        return FieldPresence::Invalid;
    }

    if (ms < 0 || ms > kMaxSafeMs) {
        return FieldPresence::Invalid;
    }
    outNs = ms * kNsPerMs;
    return FieldPresence::Ok;
}

DecodedFrame malformedFrame()
{
    return DecodedFrame { DecodeStatus::Malformed, {} };
}

DecodedFrame decodeOkxFrame(const nlohmann::json& j)
{
    const auto actionIt = j.find("action");
    const auto dataIt = j.find("data");
    if (actionIt == j.end() || dataIt == j.end() || !actionIt->is_string()
        || !dataIt->is_array() || dataIt->empty()) {
        return malformedFrame();
    }

    const auto action = actionIt->get<std::string>();
    if (action != "snapshot" && action != "update") {
        return malformedFrame();
    }

    MarketEvent event;
    event.type = action == "snapshot" ? EventType::Snapshot : EventType::Delta;

    const auto& entry = dataIt->front();
    if (!entry.is_object()) {
        return malformedFrame();
    }

    // Real OKX sequencing: seqId/prevSeqId are mandatory on books data;
    // prevSeqId may legitimately be -1 on snapshots.
    const auto seqId = parseIntField(entry, "seqId", 0,
        std::numeric_limits<std::int64_t>::max());
    if (seqId.presence != FieldPresence::Ok) {
        return malformedFrame();
    }
    const auto prevSeqId = parseIntField(entry, "prevSeqId",
        -1, std::numeric_limits<std::int64_t>::max());
    if (prevSeqId.presence != FieldPresence::Ok) {
        return malformedFrame();
    }

    event.sequence = static_cast<std::uint64_t>(seqId.value);
    event.hasSequence = true;
    event.prevSequence = prevSeqId.value;

    const auto asksIt = entry.find("asks");
    const auto bidsIt = entry.find("bids");
    if (asksIt == entry.end() || bidsIt == entry.end()) {
        return malformedFrame();
    }

    if (!parseLevels(*asksIt, event.asks) || !parseLevels(*bidsIt, event.bids)) {
        return malformedFrame();
    }

    std::int64_t timestampNs = 0;
    switch (parseTimestampMsToNs(entry, timestampNs)) {
    case FieldPresence::Ok:
        event.timestampNs = timestampNs;
        break;
    case FieldPresence::Absent:
        break;
    case FieldPresence::Invalid:
        return malformedFrame();
    }

    if (auto it = entry.find("checksum"); it != entry.end() && it->is_number_integer()) {
        event.checksum = it->get<std::int64_t>();
    }

    return DecodedFrame { DecodeStatus::Ok, std::move(event) };
}

std::optional<std::uint64_t> proxySequence(const nlohmann::json& j)
{
    const auto seq = parseIntField(j, "sequence", 0,
        std::numeric_limits<std::int64_t>::max());
    if (seq.presence != FieldPresence::Ok) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(seq.value);
}

DecodedFrame decodeProxyFrame(const nlohmann::json& j)
{
    const auto asksIt = j.find("asks");
    const auto bidsIt = j.find("bids");
    if (asksIt == j.end() || bidsIt == j.end()) {
        return malformedFrame();
    }

    MarketEvent event;

    if (const auto actionIt = j.find("action"); actionIt != j.end() && actionIt->is_string()) {
        const auto action = actionIt->get<std::string>();
        if (action != "snapshot" && action != "update") {
            return malformedFrame();
        }
        event.type = action == "snapshot" ? EventType::Snapshot : EventType::Delta;
    } else {
        event.type = EventType::Snapshot;
    }

    if (!parseLevels(*asksIt, event.asks) || !parseLevels(*bidsIt, event.bids)) {
        return malformedFrame();
    }

    std::int64_t timestampNs = 0;
    switch (parseTimestampMsToNs(j, timestampNs)) {
    case FieldPresence::Ok:
        event.timestampNs = timestampNs;
        break;
    case FieldPresence::Absent:
        break;
    case FieldPresence::Invalid:
        return malformedFrame();
    }

    if (auto it = j.find("timestamp_ns");
        it != j.end() && it->is_number_integer() && !j.contains("ts")) {
        event.timestampNs = it->get<std::int64_t>();
    }

    if (const auto seq = proxySequence(j)) {
        event.sequence = *seq;
        event.hasSequence = true;
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