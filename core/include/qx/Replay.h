#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "qx/Book.h"
#include "qx/MarketEvent.h"
#include "qx/SequenceValidator.h"

namespace qx {

struct ReplayStats {
    std::uint64_t eventsApplied = 0;
    std::uint64_t staleRejected = 0;
    std::uint64_t gapResyncs = 0;
    std::uint64_t malformedLines = 0;
    std::uint64_t lastSequence = 0;
};

struct ReplayOptions {
    double speedMultiplier = 0.0;

    // Continuity semantics applied during replay. Defaults to the OKX
    // seqId/prevSeqId model so recorded logs are judged exactly like the
    // live stream that produced them.
    SequenceValidator::Mode validatorMode = SequenceValidator::Mode::OkxSequenced;

    std::function<void(std::int64_t sleepNs)> pace;
};

ReplayStats replayInto(Book& book, const std::string& logPath, const ReplayOptions& options = {});

} // namespace qx