#include "qx/Book.h"

#include <charconv>
#include <cmath>

namespace qx {

void Book::applyLevels(Book::BidMap& side, const std::vector<Level>& levels)
{
    for (const auto& level : levels) {
        if (level.size == 0.0) {
            side.erase(level.price);
        } else {
            side[level.price] = level.size;
        }
    }
}

void Book::applyLevels(Book::AskMap& side, const std::vector<Level>& levels)
{
    for (const auto& level : levels) {
        if (level.size == 0.0) {
            side.erase(level.price);
        } else {
            side[level.price] = level.size;
        }
    }
}

void Book::applySnapshot(const MarketEvent& snapshot)
{
    const std::lock_guard<std::mutex> lock(mutex_);

    bids_.clear();
    asks_.clear();

    for (const auto& level : snapshot.bids) {
        if (level.size != 0.0 && !std::isnan(level.price)) {
            bids_[level.price] = level.size;
        }
    }
    for (const auto& level : snapshot.asks) {
        if (level.size != 0.0 && !std::isnan(level.price)) {
            asks_[level.price] = level.size;
        }
    }

    lastSequence_ = snapshot.sequence;
}

ApplyStatus Book::applyDelta(const MarketEvent& delta)
{
    const std::lock_guard<std::mutex> lock(mutex_);

    if (delta.sequence <= lastSequence_) {
        return ApplyStatus::StaleRejected;
    }
    if (delta.sequence > lastSequence_ + 1) {
        return ApplyStatus::GapRejected;
    }

    applyLevels(bids_, delta.bids);
    applyLevels(asks_, delta.asks);
    lastSequence_ = delta.sequence;

    return ApplyStatus::Applied;
}

namespace {

template <typename Map>
std::vector<Level> toLevels(const Map& side)
{
    std::vector<Level> out;
    out.reserve(side.size());
    for (const auto& [price, size] : side) {
        out.push_back(Level { price, size });
    }
    return out;
}

} // namespace

std::vector<Level> Book::bids() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return toLevels(bids_);
}

std::vector<Level> Book::asks() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return toLevels(asks_);
}

std::optional<Level> Book::bestBid() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    if (bids_.empty()) {
        return std::nullopt;
    }
    return Level { bids_.begin()->first, bids_.begin()->second };
}

std::optional<Level> Book::bestAsk() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    if (asks_.empty()) {
        return std::nullopt;
    }
    return Level { asks_.begin()->first, asks_.begin()->second };
}

std::optional<double> Book::mid() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    if (bids_.empty() || asks_.empty()) {
        return std::nullopt;
    }
    return (bids_.begin()->first + asks_.begin()->first) / 2.0;
}

std::optional<double> Book::spreadBps() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    if (bids_.empty() || asks_.empty()) {
        return std::nullopt;
    }
    const double bid = bids_.begin()->first;
    const double ask = asks_.begin()->first;
    if (bid + ask == 0.0) {
        return std::nullopt;
    }
    return ((ask - bid) / ((ask + bid) / 2.0)) * 10000.0;
}

bool Book::isCrossed() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    if (bids_.empty() || asks_.empty()) {
        return false;
    }
    return bids_.begin()->first >= asks_.begin()->first;
}

std::uint64_t Book::lastSequence() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return lastSequence_;
}

bool Book::empty() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return bids_.empty() && asks_.empty();
}

void Book::clear()
{
    const std::lock_guard<std::mutex> lock(mutex_);
    bids_.clear();
    asks_.clear();
    lastSequence_ = 0;
}

namespace {

template <typename T>
void appendNumber(std::string& out, T value)
{
    char buffer[32];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    out.append(buffer, static_cast<std::size_t>(result.ptr - buffer));
}

template <typename Map>
void appendSide(std::string& out, char tag, const Map& levels)
{
    out.push_back(tag);
    appendNumber(out, static_cast<std::uint64_t>(levels.size()));
    out.push_back('\n');
    for (const auto& [price, size] : levels) {
        appendNumber(out, price);
        out.push_back(' ');
        appendNumber(out, size);
        out.push_back('\n');
    }
}

} // namespace

std::string Book::serialize() const
{
    const std::lock_guard<std::mutex> lock(mutex_);

    std::string out;
    out.reserve(64 + 48 * (bids_.size() + asks_.size()));

    appendNumber(out, static_cast<std::uint64_t>(lastSequence_));
    out.push_back('\n');
    appendSide(out, 'B', bids_);
    appendSide(out, 'A', asks_);

    return out;
}

} // namespace qx
