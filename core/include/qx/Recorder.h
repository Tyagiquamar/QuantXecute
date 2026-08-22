#pragma once

#include <cstdint>
#include <fstream>
#include <optional>
#include <string>

#include "qx/MarketEvent.h"

namespace qx {

class Recorder {
public:
    explicit Recorder(const std::string& path);
    ~Recorder();

    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    bool record(const MarketEvent& event);

    bool isOpen() const;

private:
    std::ofstream out_;
    std::string path_;
};

class EventLogReader {
public:
    explicit EventLogReader(const std::string& path);

    std::optional<MarketEvent> next();

    std::uint64_t malformedLines() const { return malformedLines_; }
    bool eof() const { return eof_; }

private:
    std::ifstream in_;
    std::uint64_t malformedLines_ = 0;
    bool eof_ = false;
};

} // namespace qx
