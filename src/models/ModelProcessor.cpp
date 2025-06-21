#include "models/ModelProcessor.h"
#include <chrono>
#include <iostream>

void ModelProcessor::process(const UIState& input,
    OutputMetrics& output,
    const std::vector<std::pair<float, float>>& orderbook) {
    auto start = std::chrono::high_resolution_clock::now();

    // Simple simulated model logic using input and orderbook
    float topAsk = !orderbook.empty() ? orderbook.front().first : 0.0f;

    output.slippage = 0.01f * input.quantity;       // Simulated slippage
    output.fees = 0.001f * input.quantity;          // Simulated trading fee
    output.marketImpact = 0.02f * input.quantity;   // Simulated impact cost

    auto end = std::chrono::high_resolution_clock::now();
    output.latencyMs = std::chrono::duration<float, std::milli>(end - start).count();
}