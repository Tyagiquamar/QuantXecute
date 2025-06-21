#pragma once

#include <vector>
#include <utility>
#include <mutex>

class OrderBook {
public:
    void update(const std::vector<std::pair<float, float>>& bids,
        const std::vector<std::pair<float, float>>& asks);

    std::vector<std::pair<float, float>> getBids() const;
    std::vector<std::pair<float, float>> getAsks() const;

private:
    std::vector<std::pair<float, float>> bids_;
    std::vector<std::pair<float, float>> asks_;
    mutable std::mutex mutex_;
};