#pragma once

#include "InputParams.h"
#include "OutputMetrics.h"
#include "WebSocketClient.h"

class SimulationModel {
public:
    OutputMetrics runSimulation(const InputParams& input, const OrderBookData& orderBook);
};