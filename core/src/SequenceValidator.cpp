#include "qx/SequenceValidator.h"

#include <charconv>

namespace qx {

namespace {

void appendDouble(std::string& out, double value)
{
    char buffer[32];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    out.append(buffer, static_cast<std::size_t>(result.ptr - buffer));
}

} // namespace

std::uint32_t crc32(std::string_view data)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const unsigned char byte : data) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

std::int32_t computeTopOfBookChecksum(const Book& book)
{
    static constexpr std::size_t kDepth = 25;

    const auto bids = book.bids();
    const auto asks = book.asks();

    std::string payload;
    payload.reserve(kDepth * 64);

    bool first = true;
    for (std::size_t i = 0; i < kDepth; ++i) {
        if (i < bids.size()) {
            if (!first) {
                payload.push_back(':');
            }
            appendDouble(payload, bids[i].price);
            payload.push_back(':');
            appendDouble(payload, bids[i].size);
            first = false;
        }
        if (i < asks.size()) {
            if (!first) {
                payload.push_back(':');
            }
            appendDouble(payload, asks[i].price);
            payload.push_back(':');
            appendDouble(payload, asks[i].size);
            first = false;
        }
    }

    return static_cast<std::int32_t>(crc32(payload));
}

SequenceValidator::SequenceValidator(Mode mode)
    : mode_(mode)
{
}

SequenceValidator::Result SequenceValidator::validate(const MarketEvent& event)
{
    Result result;

    switch (mode_) {
    case Mode::Arrival: {
        result.verdict = Verdict::Accept;
        result.effectiveSequence = ++arrivalCounter_;
        ++stats_.accepted;
        return result;
    }

    case Mode::OkxSequenced: {
        // A snapshot always establishes a fresh baseline. Real OKX snapshots
        // carry prevSeqId = -1 ("no predecessor"); anything else is malformed
        // snapshot metadata and must not be trusted as a baseline.
        if (event.type == EventType::Snapshot) {
            if (!event.prevSequence.has_value() || *event.prevSequence != -1) {
                result.verdict = Verdict::GapResync;
                result.effectiveSequence
                    = lastSeqId_ > 0 ? static_cast<std::uint64_t>(lastSeqId_) : 0;
                ++stats_.gapResyncs;
                return result;
            }
            hasBaseline_ = true;
            lastSeqId_ = static_cast<std::int64_t>(event.sequence);
            result.verdict = Verdict::Accept;
            result.effectiveSequence = event.sequence;
            ++stats_.accepted;
            return result;
        }

        // Deltas before a snapshot cannot be anchored to any book state.
        // This is expected churn right after subscribing, not a feed
        // discontinuity: drop without counting a gap.
        if (!hasBaseline_) {
            result.verdict = Verdict::GapResync;
            result.effectiveSequence = 0;
            return result;
        }

        // Without prevSeqId metadata continuity is unverifiable: reject and
        // resynchronize rather than guess.
        if (!event.prevSequence.has_value()) {
            result.verdict = Verdict::GapResync;
            result.effectiveSequence = static_cast<std::uint64_t>(lastSeqId_);
            ++stats_.gapResyncs;
            return result;
        }

        const std::int64_t prevSeqId = *event.prevSequence;

        // The single real-OKX continuity rule: the message must connect to
        // the previously accepted seqId. seqId itself may jump forward,
        // repeat (no-change keepalive) or move lower (maintenance reset).
        if (prevSeqId != lastSeqId_) {
            result.verdict = Verdict::GapResync;
            result.effectiveSequence = static_cast<std::uint64_t>(lastSeqId_);
            ++stats_.gapResyncs;
            return result;
        }

        // A repeated seqId is only valid in the no-change keepalive form
        // (empty asks and bids). A repeated seqId carrying levels would
        // double-apply those levels.
        if (static_cast<std::int64_t>(event.sequence) == lastSeqId_
            && !(event.bids.empty() && event.asks.empty())) {
            result.verdict = Verdict::StaleReject;
            result.effectiveSequence = static_cast<std::uint64_t>(lastSeqId_);
            ++stats_.staleRejected;
            return result;
        }

        lastSeqId_ = static_cast<std::int64_t>(event.sequence);
        result.verdict = Verdict::Accept;
        result.effectiveSequence = event.sequence;
        ++stats_.accepted;
        return result;
    }
    }

    return result;
}

SequenceValidator::ChecksumVerdict SequenceValidator::verifyChecksum(const Book& book,
    const MarketEvent& applied,
    IntegrityPolicy policy)
{
    switch (policy) {
    case IntegrityPolicy::SequenceOnly:
        // Current OKX books channels deprecated the checksum field on
        // 2026-06-23; it remains present but is fixed to 0. Never treat that
        // as verification data - continuity comes from seqId/prevSeqId.
        return ChecksumVerdict::NotPresent;

    case IntegrityPolicy::SequenceAndChecksum:
        break;
    }

    if (!applied.checksum.has_value()) {
        return ChecksumVerdict::NotPresent;
    }

    const std::int32_t expected = computeTopOfBookChecksum(book);
    if (expected == static_cast<std::int32_t>(*applied.checksum)) {
        return ChecksumVerdict::Pass;
    }

    ++stats_.checksumFailures;
    return ChecksumVerdict::Mismatch;
}

void SequenceValidator::reset()
{
    stats_ = Stats {};
    hasBaseline_ = false;
    lastSeqId_ = -1;
    arrivalCounter_ = 0;
}

}