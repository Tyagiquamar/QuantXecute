#include "models/SlippageModel.h"
#include <vector>
#include <string>
#include <functional> 
void SlippageModel::fit(const std::vector<double>& x, const std::vector<double>& y) {
    double sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;
    size_t n = x.size();
    for (size_t i = 0; i < n; ++i) {
        sumX += x[i];
        sumY += y[i];
        sumXY += x[i] * y[i];
        sumX2 += x[i] * x[i];
    }
    slope = (n * sumXY - sumX * sumY) / (n * sumX2 - sumX * sumX);
    intercept = (sumY - slope * sumX) / n;
}

double SlippageModel::predict(double quantity) const {
    return slope * quantity + intercept;
}