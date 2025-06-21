#include "models/SimulationModel.h"
#include <iostream>
#include <algorithm>
#include <chrono>
#include <cmath>

OutputMetrics SimulationModel::runSimulation(const InputParams& input, const OrderBookData& orderBook) {
    OutputMetrics output;

    auto start = std::chrono::high_resolution_clock::now();  // Start the timer

    if (orderBook.asks.empty()) {
        std::cerr << "Order book is empty. Cannot simulate.\n";
        auto end = std::chrono::high_resolution_clock::now();
        output.latencyMs = std::chrono::duration<double, std::milli>(end - start).count();
        return output;
    }

    double sizeToBuy = input.tradeAmount;
    double cost = 0.0;
    double filled = 0.0;

    for (const auto& ask : orderBook.asks) {
        if (filled >= sizeToBuy) break;
        double fill = std::min(ask.size, sizeToBuy - filled);
        cost += fill * ask.price;
        filled += fill;
    }

    if (filled == 0.0) {
        std::cerr << "Not enough liquidity\n";
        auto end = std::chrono::high_resolution_clock::now();
        output.latencyMs = std::chrono::duration<double, std::milli>(end - start).count();
        return output;
    }

    output.slippage = (cost / filled) - orderBook.asks[0].price;
    output.fees = cost * input.feeRate;
    output.marketImpact = std::abs(output.slippage) * sizeToBuy;
    output.netCost = output.slippage + output.fees + output.marketImpact;

    double spread = orderBook.asks[0].price - orderBook.bids[0].price;
    output.makerTakerRatio = std::max(0.0, std::min(1.0,
        1.0 / (1.0 + std::exp(-10.0 * spread))));

    auto end = std::chrono::high_resolution_clock::now();  // End the timer
    output.latencyMs = std::chrono::duration<double, std::milli>(end - start).count();

    return output;
}