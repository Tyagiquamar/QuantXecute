#pragma once
#include <vector>
#include <utility>

struct OrderBook {
    std::vector<std::pair<double, double>> asks;
    std::vector<std::pair<double, double>> bids;
    std::string timestamp;
};