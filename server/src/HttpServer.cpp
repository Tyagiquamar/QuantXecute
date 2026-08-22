#include "qx/server/HttpServer.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <optional>
#include <sstream>

namespace qx::server {

namespace {

// Multiplications done in size_t so the limits never widen from int (clang-tidy bugprone).
constexpr std::size_t kMaxHeaderBytes = std::size_t{1024} * 1024;    // 1 MiB request headers
constexpr std::size_t kMaxBodyBytes = std::size_t{8} * 1024 * 1024;  // 8 MiB request body

std::string statusText(int status)
{
    switch (status) {
    case 101: return "Switching Protocols";
    case 200: return "OK";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 426: return "Upgrade Required";
    default: return "Error";
    }
}

bool readUntilHeadersComplete(int fd, std::string& buffer)
{
    char chunk[4096];
    while (buffer.find("\r\n\r\n") == std::string::npos) {
        const ssize_t received = ::recv(fd, chunk, sizeof(chunk), 0);
        if (received <= 0) {
            return false;
        }
        buffer.append(chunk, static_cast<std::size_t>(received));
        if (buffer.size() > kMaxHeaderBytes) {
            return false;
        }
    }
    return true;
}

bool readBody(int fd, std::string& buffer)
{
    const auto headerEnd = buffer.find("\r\n\r\n");
    std::istringstream headers(buffer.substr(0, headerEnd));
    std::string line;
    std::size_t contentLength = 0;
    bool haveLength = false;
    while (std::getline(headers, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        std::string key = line.substr(0, colon);
        for (auto& character : key) {
            character = static_cast<char>(::tolower(character));
        }
        if (key == "content-length") {
            contentLength = static_cast<std::size_t>(std::stoull(line.substr(colon + 1)));
            haveLength = true;
        }
    }
    if (!haveLength) {
        return true;
    }

    const std::size_t totalNeeded = headerEnd + 4 + contentLength;
    char chunk[4096];
    while (buffer.size() < totalNeeded) {
        const ssize_t received = ::recv(fd, chunk, sizeof(chunk), 0);
        if (received <= 0) {
            return false;
        }
        buffer.append(chunk, static_cast<std::size_t>(received));
        if (buffer.size() > kMaxBodyBytes) {
            return false;
        }
    }
    return true;
}

void sha1(const uint8_t* message, std::size_t length, uint8_t digest[20])
{
    std::uint32_t h0 = 0x67452301u;
    std::uint32_t h1 = 0xEFCDAB89u;
    std::uint32_t h2 = 0x98BADCFEu;
    std::uint32_t h3 = 0x10325476u;
    std::uint32_t h4 = 0xC3D2E1F0u;

    const std::uint64_t bitLength = static_cast<std::uint64_t>(length) * 8u;

    std::vector<uint8_t> data(message, message + length);
    data.push_back(0x80u);
    while (data.size() % 64u != 56u) {
        data.push_back(0x00u);
    }
    for (int shift = 56; shift >= 0; shift -= 8) {
        data.push_back(static_cast<uint8_t>((bitLength >> shift) & 0xFFu));
    }

    for (std::size_t blockStart = 0; blockStart < data.size(); blockStart += 64u) {
        std::uint32_t w[80];
        for (int t = 0; t < 16; ++t) {
            const std::size_t offset = blockStart + static_cast<std::size_t>(t) * 4u;
            w[t] = (static_cast<std::uint32_t>(data[offset]) << 24)
                | (static_cast<std::uint32_t>(data[offset + 1]) << 16)
                | (static_cast<std::uint32_t>(data[offset + 2]) << 8)
                | static_cast<std::uint32_t>(data[offset + 3]);
        }
        for (int t = 16; t < 80; ++t) {
            const std::uint32_t value = w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16];
            w[t] = (value << 1u) | (value >> 31u);
        }

        std::uint32_t a = h0;
        std::uint32_t b = h1;
        std::uint32_t c = h2;
        std::uint32_t d = h3;
        std::uint32_t e = h4;

        for (int t = 0; t < 80; ++t) {
            std::uint32_t f;
            std::uint32_t k;
            if (t < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999u;
            } else if (t < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            } else if (t < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6u;
            }
            const std::uint32_t temp = ((a << 5u) | (a >> 27u)) + f + e + k + w[t];
            e = d;
            d = c;
            c = (b << 30u) | (b >> 2u);
            b = a;
            a = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    const std::uint32_t hs[5] = { h0, h1, h2, h3, h4 };
    for (int i = 0; i < 5; ++i) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            digest[i * 4 + (24 - shift) / 8] = static_cast<uint8_t>((hs[i] >> shift) & 0xFFu);
        }
    }
}

std::string base64Encode(const uint8_t* data, std::size_t length)
{
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((length + 2u) / 3u) * 4u);

    for (std::size_t i = 0; i < length; i += 3u) {
        const std::uint32_t triple = (static_cast<std::uint32_t>(data[i]) << 16)
            | (i + 1u < length ? static_cast<std::uint32_t>(data[i + 1]) << 8 : 0u)
            | (i + 2u < length ? static_cast<std::uint32_t>(data[i + 2]) : 0u);

        out += table[(triple >> 18) & 0x3F];
        out += table[(triple >> 12) & 0x3F];
        out += i + 1u < length ? table[(triple >> 6) & 0x3F] : '=';
        out += i + 2u < length ? table[triple & 0x3F] : '=';
    }
    return out;
}

bool sendAll(int fd, const void* data, std::size_t length)
{
    const char* bytes = static_cast<const char*>(data);
    std::size_t sent = 0;
    while (sent < length) {
        const ssize_t written = ::send(fd, bytes + sent, length - sent, MSG_NOSIGNAL);
        if (written <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(written);
    }
    return true;
}

} // namespace

HttpServer::~HttpServer()
{
    stop();
}

bool HttpServer::start(const std::string& bindAddress, int port)
{
    ::signal(SIGPIPE, SIG_IGN);

    listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        return false;
    }

    int reuse = 1;
    ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(port));
    if (::inet_pton(AF_INET, bindAddress.c_str(), &address.sin_addr) != 1) {
        address.sin_addr.s_addr = INADDR_ANY;
    }

    if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0
        || ::listen(listenFd_, 64) != 0) {
        ::close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    running_ = true;
    acceptThread_ = std::thread([this] { acceptLoop(); });
    return true;
}

void HttpServer::stop()
{
    if (!running_.exchange(false)) {
        return;
    }
    if (listenFd_ >= 0) {
        ::shutdown(listenFd_, SHUT_RDWR);
        ::close(listenFd_);
        listenFd_ = -1;
    }
    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }
}

void HttpServer::acceptLoop()
{
    while (running_) {
        const int clientFd = ::accept(listenFd_, nullptr, nullptr);
        if (clientFd < 0) {
            break;
        }
        std::thread([this, clientFd] { handleConnection(clientFd); }).detach();
    }
}

void HttpServer::handleConnection(int clientFd)
{
    std::string buffer;
    if (!readUntilHeadersComplete(clientFd, buffer) || !readBody(clientFd, buffer)) {
        ::close(clientFd);
        return;
    }

    HttpRequest request;
    const auto headerEnd = buffer.find("\r\n\r\n");
    std::istringstream head(buffer.substr(0, headerEnd));

    std::string requestLine;
    std::getline(head, requestLine);
    {
        std::istringstream parts(requestLine);
        parts >> request.method >> request.path;
    }

    std::string headerLine;
    while (std::getline(head, headerLine)) {
        const auto colon = headerLine.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        std::string key = headerLine.substr(0, colon);
        std::string value = headerLine.substr(colon + 1);
        for (auto& character : key) {
            character = static_cast<char>(::tolower(character));
        }
        while (!value.empty() && value.front() == ' ') {
            value.erase(value.begin());
        }
        request.headers[key] = value;
    }

    request.body = buffer.substr(headerEnd + 4);

    const std::string upgrade = request.header("upgrade");
    if (upgrade == "websocket" && webSocketHandler_) {
        const std::string key = request.header("sec-websocket-key");
        if (key.empty() || request.path != "/events") {
            const HttpResponse response { 400, "application/json", "{}", {} };
            std::ostringstream out;
            out << "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
            const std::string bytes = out.str();
            (void)sendAll(clientFd, bytes.data(), bytes.size());
            ::close(clientFd);
            return;
        }

        const std::string acceptInput = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        uint8_t digest[20];
        sha1(reinterpret_cast<const uint8_t*>(acceptInput.data()), acceptInput.size(), digest);

        std::ostringstream upgradeResponse;
        upgradeResponse << "HTTP/1.1 101 Switching Protocols\r\n"
                        << "Upgrade: websocket\r\n"
                        << "Connection: Upgrade\r\n"
                        << "Sec-WebSocket-Accept: " << base64Encode(digest, 20) << "\r\n"
                        << "\r\n";
        const std::string bytes = upgradeResponse.str();
        if (!sendAll(clientFd, bytes.data(), bytes.size())) {
            ::close(clientFd);
            return;
        }

        webSocketHandler_(clientFd);
        ::close(clientFd);
        return;
    }

    HttpResponse response;
    if (!handler_ || request.method.empty() || request.path.empty()
        || request.path.find(' ') != std::string::npos
        || request.path.rfind('/', 0) != 0) {
        response.status = 400;
        response.body = "{\"error\":\"malformed request\"}";
    } else {
        response = handler_(request);
    }

    std::ostringstream out;
    out << "HTTP/1.1 " << response.status << " " << statusText(response.status) << "\r\n"
        << "Content-Type: " << response.contentType << "\r\n"
        << "Content-Length: " << response.body.size() << "\r\n"
        << "Access-Control-Allow-Origin: *\r\n";
    for (const auto& [key, value] : response.extraHeaders) {
        out << key << ": " << value << "\r\n";
    }
    out << "Connection: close\r\n\r\n" << response.body;

    const std::string bytes = out.str();
    (void)sendAll(clientFd, bytes.data(), bytes.size());
    ::close(clientFd);
}

} // namespace qx::server
