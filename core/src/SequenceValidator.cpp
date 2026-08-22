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

    case Mode::Sequenced: {
        if (event.type == EventType::Snapshot) {
            lastSequence_ = event.sequence;
            result.verdict = Verdict::Accept;
            result.effectiveSequence = event.sequence;
            ++stats_.accepted;
            return result;
        }

        if (event.sequence <= lastSequence_) {
            result.verdict = Verdict::StaleReject;
            result.effectiveSequence = lastSequence_;
            ++stats_.staleRejected;
            return result;
        }

        if (event.sequence > lastSequence_ + 1) {
            result.verdict = Verdict::GapResync;
            result.effectiveSequence = lastSequence_;
            ++stats_.gapResyncs;
            return result;
        }

        lastSequence_ = event.sequence;
        result.verdict = Verdict::Accept;
        result.effectiveSequence = event.sequence;
        ++stats_.accepted;
        return result;
    }
    }

    return result;
}

bool SequenceValidator::verifyChecksum(const Book& book, const MarketEvent& applied)
{
    if (!applied.checksum.has_value()) {
        return true;
    }

    const std::int32_t expected = computeTopOfBookChecksum(book);
    if (expected == static_cast<std::int32_t>(*applied.checksum)) {
        return true;
    }

    ++stats_.checksumFailures;
    return false;
}

void SequenceValidator::reset()
{
    stats_ = Stats {};
    lastSequence_ = 0;
    arrivalCounter_ = 0;
}

}
