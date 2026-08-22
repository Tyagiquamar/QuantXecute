#pragma once

#include <string_view>

#include "qx/MarketEvent.h"

namespace qx {

enum class FeedFormat : std::uint8_t {
    // Real OKX v5 order-book framing: action snapshot/update, data[0] carrying
    // seqId/prevSeqId sequencing metadata.
    OkxBooks,

    // Generic proxy framing without transport sequencing guarantees; pair with
    // SequenceValidator::Mode::Arrival (documented weaker integrity).
    ProxyBooks,
};

enum class DecodeStatus : std::uint8_t {
    Ok,
    Malformed,
};

struct DecodedFrame {
    DecodeStatus status = DecodeStatus::Malformed;
    MarketEvent event;
};

DecodedFrame decodeFrame(std::string_view payload, FeedFormat format);

}
