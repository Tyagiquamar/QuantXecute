#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <thread>

namespace qx::server {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string body;
    std::map<std::string, std::string> headers;

    std::string header(const std::string& name) const
    {
        const auto it = headers.find(name);
        return it == headers.end() ? std::string {} : it->second;
    }
};

struct HttpResponse {
    int status = 200;
    std::string contentType = "application/json";
    std::string body;
    std::map<std::string, std::string> extraHeaders;
};

class HttpServer {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    using WebSocketSession = std::function<void(int fd)>;

    ~HttpServer();

    bool start(const std::string& bindAddress, int port);
    void stop();
    void setHandler(Handler handler) { handler_ = std::move(handler); }
    void setWebSocketHandler(WebSocketSession session) { webSocketHandler_ = std::move(session); }

private:
    void acceptLoop();
    void handleConnection(int clientFd);

    int listenFd_ = -1;
    std::atomic<bool> running_ { false };
    std::thread acceptThread_;
    Handler handler_;
    WebSocketSession webSocketHandler_;
};

} // namespace qx::server
