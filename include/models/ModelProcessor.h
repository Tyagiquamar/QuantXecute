#pragma once

#include "ui/UIState.h"
#include "models/OutputMetrics.h"
#include <vector>
#include <utility>

class ModelProcessor {
public:
    void process(const UIState& input,
        OutputMetrics& output,
        const std::vector<std::pair<float, float>>& orderbook);
};