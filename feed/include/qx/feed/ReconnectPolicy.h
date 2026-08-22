#pragma once

#include <chrono>
#include <cstdint>

namespace qx::feed {

class ReconnectPolicy {
public:
    ReconnectPolicy(std::chrono::milliseconds baseDelay,
        std::chrono::milliseconds maxDelay,
        double jitterFraction);

    static ReconnectPolicy defaults();

    std::chrono::milliseconds delayForAttempt(std::uint32_t attempt) const;

private:
    std::chrono::milliseconds baseDelay_;
    std::chrono::milliseconds maxDelay_;
    double jitterFraction_;
    mutable std::uint32_t seed_ = 0x9E3779B9u;
};

} // namespace qx::feed
