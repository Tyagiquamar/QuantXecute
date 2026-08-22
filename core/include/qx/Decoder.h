#pragma once

#include <string_view>

#include "qx/MarketEvent.h"

namespace qx {

enum class FeedFormat : std::uint8_t {
    OkxBooks,
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
