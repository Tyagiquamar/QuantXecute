#pragma once

#include <ixwebsocket/IXWebSocket.h>
#include <mutex>
#include <string>
#include <vector>

struct OrderBookEntry {
    double price;
    double size;
};

struct OrderBookData {
    std::vector<OrderBookEntry> bids;
    std::vector<OrderBookEntry> asks;
};

class WebSocketClient {
public:
    WebSocketClient();
    ~WebSocketClient();

    void connect(const std::string& symbol);
    OrderBookData getLatestOrderBook();
    bool isOrderBookReady() const;

private:
    ix::WebSocket webSocket;
    OrderBookData latestOrderBook;
    std::mutex dataMutex;
    bool hasReceivedOrderBook;
};