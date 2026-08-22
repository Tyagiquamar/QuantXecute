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
    std::uint64_t sequence = 0;
    std::int64_t timestampNs = 0;

    std::vector<Level> bids;
    std::vector<Level> asks;

    std::optional<std::uint32_t> checksum;
};

constexpr bool operator==(const MarketEvent& lhs, const MarketEvent& rhs) noexcept
{
    if (lhs.type != rhs.type || lhs.sequence != rhs.sequence
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
