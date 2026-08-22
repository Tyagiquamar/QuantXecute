#pragma once

struct OutputMetrics {
    float slippage = 0.0f;
    float fees = 0.0f;
    float marketImpact = 0.0f;
    
    float netCost = 0.0f;
    float makerTakerRatio = 0.0f;
    double latencyMs = 0.0;
};