#pragma once
#include <string>

struct InputParams {
    std::string exchange = "OKX"; 
    std::string spotAsset = "BTC-USDT";
    std::string orderType = "market"; 
    float tradeAmount = 100.0f;
    float volatility = 0.05f;
    float feeRate = 0.1f;

    bool shouldSimulate = false;
};