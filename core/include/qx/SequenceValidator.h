#pragma once

#include <cstdint>
#include <string_view>

#include "qx/Book.h"
#include "qx/MarketEvent.h"

namespace qx {

std::uint32_t crc32(std::string_view data);

std::int32_t computeTopOfBookChecksum(const Book& book);

class SequenceValidator {
public:
    enum class Mode : std::uint8_t {
        Sequenced,
        Arrival,
    };

    enum class Verdict : std::uint8_t {
        Accept,
        StaleReject,
        GapResync,
    };

    struct Result {
        Verdict verdict = Verdict::StaleReject;
        std::uint64_t effectiveSequence = 0;
    };

    struct Stats {
        std::uint64_t accepted = 0;
        std::uint64_t staleRejected = 0;
        std::uint64_t gapResyncs = 0;
        std::uint64_t checksumFailures = 0;
    };

    explicit SequenceValidator(Mode mode = Mode::Sequenced);

    Result validate(const MarketEvent& event);

    bool verifyChecksum(const Book& book, const MarketEvent& applied);

    const Stats& stats() const { return stats_; }

    void reset();

private:
    Mode mode_;
    Stats stats_;
    std::uint64_t lastSequence_ = 0;
    std::uint64_t arrivalCounter_ = 0;
};

}
