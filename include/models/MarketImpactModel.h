#pragma once
#include <vector>
#include <string>
#include <functional> 
class MarketImpactModel {
public:
    double computeImpact(double quantity, double volatility, double timeHorizon);
};