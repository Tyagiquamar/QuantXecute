#pragma once
#include <functional>
#include "OrderBook.h"

class WebSocketClient {
public:
    void start(const std::string& url, std::function<void(const OrderBook&)> onUpdate);
    void stop();
};