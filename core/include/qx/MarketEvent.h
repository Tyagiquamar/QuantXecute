#pragma once

#include <cstdint>
#include <cstddef>
#include <optional>
#include <vector>

#include "qx/Level.h"

namespace qx {

enum class EventType : std::uint8_t {
    Snapshot,
    Delta,
};

struct MarketEvent {
    EventType type = EventType::Delta;

    // Feed sequence id carried by the transport (OKX seqId). Absent metadata
    // means the feed provides no usable sequencing (Arrival mode).
    std::uint64_t sequence = 0;
    bool hasSequence = false;

    // Previous sequence id (OKX prevSeqId). Signed because OKX snapshots use
    // the sentinel -1 ("no predecessor"); never store it in an unsigned type.
    std::optional<std::int64_t> prevSequence;

    std::int64_t timestampNs = 0;

    std::vector<Level> bids;
    std::vector<Level> asks;

    std::optional<std::int64_t> checksum;
};

constexpr bool operator==(const MarketEvent& lhs, const MarketEvent& rhs) noexcept
{
    if (lhs.type != rhs.type || lhs.sequence != rhs.sequence
        || lhs.hasSequence != rhs.hasSequence || lhs.prevSequence != rhs.prevSequence
        || lhs.timestampNs != rhs.timestampNs || lhs.checksum != rhs.checksum
        || lhs.bids.size() != rhs.bids.size() || lhs.asks.size() != rhs.asks.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.bids.size(); ++i) {
        if (!(lhs.bids[i] == rhs.bids[i])) {
            return false;
        }
    }
    for (std::size_t i = 0; i < lhs.asks.size(); ++i) {
        if (!(lhs.asks[i] == rhs.asks[i])) {
            return false;
        }
    }
    return true;
}

constexpr bool operator!=(const MarketEvent& lhs, const MarketEvent& rhs) noexcept
{
    return !(lhs == rhs);
}

} // namespace qx
