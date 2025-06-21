#include "models/OrderBook.h"

void OrderBook::update(const std::vector<std::pair<float, float>>& bids,
    const std::vector<std::pair<float, float>>& asks) {
    std::lock_guard<std::mutex> lock(mutex_);
    bids_ = bids;
    asks_ = asks;
}

std::vector<std::pair<float, float>> OrderBook::getBids() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bids_;
}

std::vector<std::pair<float, float>> OrderBook::getAsks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return asks_;
}