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

enum class ApplyStatus : std::uint8_t {
    Applied,
    StaleRejected,
    GapRejected,
};

class Book {
public:
    Book() = default;

    void applySnapshot(const MarketEvent& snapshot);

    ApplyStatus applyDelta(const MarketEvent& delta);

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

    static void applyLevels(BidMap& side, const std::vector<Level>& levels);
    static void applyLevels(AskMap& side, const std::vector<Level>& levels);

    mutable std::mutex mutex_;
    BidMap bids_;
    AskMap asks_;
    std::uint64_t lastSequence_ = 0;
};

} // namespace qx
