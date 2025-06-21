#include "models/MarketImpactModel.h"
#include <cmath>
#include <vector>
#include <string>
#include <functional> 

double MarketImpactModel::computeImpact(double quantity, double volatility, double timeHorizon) {
    return volatility * std::sqrt(quantity / timeHorizon);
}