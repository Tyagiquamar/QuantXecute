#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "qx/Book.h"
#include "qx/MarketEvent.h"

namespace qx {

struct ReplayStats {
    std::uint64_t eventsApplied = 0;
    std::uint64_t staleRejected = 0;
    std::uint64_t malformedLines = 0;
    std::uint64_t lastSequence = 0;
};

struct ReplayOptions {
    double speedMultiplier = 0.0;

    std::function<void(std::int64_t sleepNs)> pace;
};

ReplayStats replayInto(Book& book, const std::string& logPath, const ReplayOptions& options = {});

} // namespace qx
