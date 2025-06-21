#pragma once
enum class FeeTier { Tier1, Tier2, Tier3 };
#include <vector>
#include <string>
#include <functional> 

class FeeModel {
public:
    static double calculateFee(double amountUSD, FeeTier tier);
};