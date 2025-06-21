#include "models/WebSocketClient.h"
#include <nlohmann/json.hpp>
#include <iostream>

using json = nlohmann::json;

WebSocketClient::WebSocketClient() : hasReceivedOrderBook(false) {}

WebSocketClient::~WebSocketClient() {
    webSocket.stop();
}

void WebSocketClient::connect(const std::string& symbol) {
    std::string url = "wss://ws.gomarket-cpp.goquant.io/ws/l2-orderbook/okx/" + symbol;
    webSocket.setUrl(url);

    webSocket.setOnMessageCallback([this, symbol](const ix::WebSocketMessagePtr& msg) {
        switch (msg->type) {
        case ix::WebSocketMessageType::Open:
            std::cout << "WebSocket connection opened successfully.\n";

            {
                json subscribeMessage = {
                    {"op", "subscribe"},
                    {"args", {{
                        {"channel", "books"},
                        {"instId", symbol}
                    }}}
                };
                std::cout << "Sending subscription: " << subscribeMessage.dump() << std::endl;
                webSocket.send(subscribeMessage.dump());
            }
            break;

        case ix::WebSocketMessageType::Close:
            std::cout << "WebSocket connection closed. Code: " << msg->closeInfo.code
                << " Reason: " << msg->closeInfo.reason << std::endl;
            break;

        case ix::WebSocketMessageType::Error:
            std::cerr << "WebSocket error: " << msg->errorInfo.reason << std::endl;
            break;

        case ix::WebSocketMessageType::Message:
            try {
                std::cout << "===== RAW MESSAGE RECEIVED =====\n";
                std::cout << msg->str << "\n";

                json j = json::parse(msg->str);
                std::cout << "===== PARSED JSON STRUCTURE =====\n";
                std::cout << j.dump(2) << std::endl;

                if (j.contains("asks") && j.contains("bids")) {
                    std::lock_guard<std::mutex> lock(dataMutex);

                    latestOrderBook.bids.clear();
                    latestOrderBook.asks.clear();

                    for (const auto& bid : j["bids"]) {
                        latestOrderBook.bids.push_back({
                            std::stod(bid[0].get<std::string>()),
                            std::stod(bid[1].get<std::string>())
                            });
                    }

                    for (const auto& ask : j["asks"]) {
                        latestOrderBook.asks.push_back({
                            std::stod(ask[0].get<std::string>()),
                            std::stod(ask[1].get<std::string>())
                            });
                    }

                    hasReceivedOrderBook = true;
                    std::cout << "Order book updated: "
                        << latestOrderBook.bids.size() << " bids, "
                        << latestOrderBook.asks.size() << " asks" << std::endl;
                }
                else {
                    std::cerr << "Message did not match expected order book structure.\n";
                }
            }
            catch (const std::exception& e) {
                std::cerr << "Failed to parse order book message: " << e.what() << std::endl;
            }
            break;

        default:
            break;
        }
        });

    webSocket.start();
}

OrderBookData WebSocketClient::getLatestOrderBook() {
    std::lock_guard<std::mutex> lock(dataMutex);
    return latestOrderBook;
}

bool WebSocketClient::isOrderBookReady() const {
    return hasReceivedOrderBook;
}