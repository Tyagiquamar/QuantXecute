#pragma once

#include <cstddef>
#include <cstdint>

namespace qx {

struct Level {
    double price = 0.0;
    double size = 0.0;
};

constexpr bool operator==(const Level& lhs, const Level& rhs) noexcept
{
    return lhs.price == rhs.price && lhs.size == rhs.size;
}

constexpr bool operator!=(const Level& lhs, const Level& rhs) noexcept
{
    return !(lhs == rhs);
}

} // namespace qx
