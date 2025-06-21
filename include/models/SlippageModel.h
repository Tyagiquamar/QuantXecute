#pragma once
#include <vector>
#include <string>
#include <functional>

class SlippageModel {
public:
    void fit(const std::vector<double>& x, const std::vector<double>& y);
    double predict(double quantity) const;

private:
    double slope = 0.0;
    double intercept = 0.0;
};