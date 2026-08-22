#pragma once
#include <string>

struct UIState {
    std::string exchange = "OKX";
    std::string asset = "BTC-USDT-SWAP";
    int orderTypeIndex = 0;
    float quantity = 100.0f;
    float volatility = 0.1f;
    int feeTier = 1;
};