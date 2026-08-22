#pragma once

#include <cstdint>
#include <string_view>

#include "qx/Book.h"
#include "qx/MarketEvent.h"

namespace qx {

std::uint32_t crc32(std::string_view data);

std::int32_t computeTopOfBookChecksum(const Book& book);

// Which integrity guarantees a configured feed actually provides.
//
// - SequenceOnly: continuity is enforced through seqId/prevSeqId alone. This is
//   the correct policy for current OKX `books` channels, whose deprecated
//   checksum field is fixed to 0 since 2026-06-23 and must not be verified.
// - SequenceAndChecksum: the feed supplies a meaningful per-message checksum;
//   mismatches invalidate the book.
enum class IntegrityPolicy : std::uint8_t {
    SequenceOnly,
    SequenceAndChecksum,
};

class SequenceValidator {
public:
    enum class Mode : std::uint8_t {
        // OKX v5 order-book semantics: a delta is accepted iff its prevSeqId
        // equals the previously accepted seqId. seqId itself may jump forward,
        // stay identical (no-change keepalive) or move lower (maintenance
        // reset). Snapshots carry prevSeqId = -1.
        OkxSequenced,

        // Feeds without usable transport sequencing: every event is accepted
        // and stamped with a local monotonic counter. Weaker guarantee,
        // documented as such.
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

    enum class ChecksumVerdict : std::uint8_t {
        NotPresent,
        Pass,
        Mismatch,
    };

    explicit SequenceValidator(Mode mode = Mode::OkxSequenced);

    Result validate(const MarketEvent& event);

    // Verify a genuinely-supplied checksum against the applied book. Under
    // SequenceOnly the deprecated OKX checksum=0 field is ignored entirely.
    ChecksumVerdict verifyChecksum(const Book& book,
        const MarketEvent& applied,
        IntegrityPolicy policy);

    bool hasBaseline() const;

    const Stats& stats() const { return stats_; }

    void reset();

private:
    Mode mode_;
    Stats stats_;
    bool hasBaseline_ = false;
    std::int64_t lastSeqId_ = -1;
    std::uint64_t arrivalCounter_ = 0;
};

}
