#include "models/FeeModel.h"
#include <vector>
#include <string>
#include <functional> 

double FeeModel::calculateFee(double amountUSD, FeeTier tier) {
    switch (tier) {
    case FeeTier::Tier1: return amountUSD * 0.001;
    case FeeTier::Tier2: return amountUSD * 0.0007;
    case FeeTier::Tier3: return amountUSD * 0.0005;
    default: return amountUSD * 0.001;
    }
}