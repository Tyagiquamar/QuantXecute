#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "qx/Level.h"
#include "qx/MarketEvent.h"

namespace qx {

// The Book is a pure state applier: it never judges continuity. Feed-specific
// sequencing rules live in SequenceValidator, and an event reaches this class
// only after that validator accepted it. This keeps OKX-valid updates (forward
// seqId jumps, no-change keepalives, maintenance resets) from being rejected
// here by stale generic assumptions.
class Book {
public:
    Book() = default;

    void applySnapshot(const MarketEvent& snapshot);

    void applyDelta(const MarketEvent& delta);

    std::vector<Level> bids() const;
    std::vector<Level> asks() const;

    std::optional<Level> bestBid() const;
    std::optional<Level> bestAsk() const;
    std::optional<double> mid() const;
    std::optional<double> spreadBps() const;

    bool isCrossed() const;
    std::uint64_t lastSequence() const;
    bool empty() const;
    void clear();

    std::string serialize() const;

private:
    using BidMap = std::map<double, double, std::greater<double>>;
    using AskMap = std::map<double, double, std::less<double>>;

    template <typename Map>
    static void applyLevels(Map& side, const std::vector<Level>& levels)
    {
        for (const auto& level : levels) {
            if (level.size == 0.0) {
                side.erase(level.price);
            } else {
                side[level.price] = level.size;
            }
        }
    }

    mutable std::mutex mutex_;
    BidMap bids_;
    AskMap asks_;
    std::uint64_t lastSequence_ = 0;
};

} // namespace qx
