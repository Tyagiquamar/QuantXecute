#include "qx/feed/ReconnectPolicy.h"

#include <algorithm>
#include <cmath>

namespace qx::feed {

namespace {

std::uint32_t xorshift32(std::uint32_t& state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

double jitter01(std::uint32_t& state)
{
    return static_cast<double>(xorshift32(state) & 0xFFFFu) / 65536.0;
}

} // namespace

ReconnectPolicy::ReconnectPolicy(
    std::chrono::milliseconds baseDelay,
    std::chrono::milliseconds maxDelay,
    double jitterFraction)
    : baseDelay_(baseDelay)
    , maxDelay_(maxDelay)
    , jitterFraction_(jitterFraction)
{
}

ReconnectPolicy ReconnectPolicy::defaults()
{
    static thread_local std::uint32_t seed = [] {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        return static_cast<std::uint32_t>(now) | 1u;
    }();

    ReconnectPolicy policy { std::chrono::milliseconds(500),
        std::chrono::milliseconds(30000), 0.25 };
    policy.seed_ = seed;
    seed = static_cast<std::uint32_t>(seed * 1664525u + 1013904223u) | 1u;
    return policy;
}

std::chrono::milliseconds ReconnectPolicy::delayForAttempt(std::uint32_t attempt) const
{
    const double exponent = std::min<double>(static_cast<double>(attempt), 20.0);
    const double raw = static_cast<double>(baseDelay_.count()) * std::pow(2.0, exponent);

    const auto delay = std::chrono::milliseconds{static_cast<std::int64_t>(
        std::min(raw, static_cast<double>(maxDelay_.count())))};

    const double jitter = jitter01(seed_) * jitterFraction_;
    return std::chrono::milliseconds{std::max<std::int64_t>(
        static_cast<std::int64_t>(static_cast<double>(delay.count()) * (1.0 - jitter)), 0)};
}

} // namespace qx::feed
