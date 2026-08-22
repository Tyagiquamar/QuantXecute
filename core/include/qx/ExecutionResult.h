#pragma once

#include <cstddef>
#include <cstdint>

namespace qx {

enum class Side : std::uint8_t {
    Buy,
    Sell,
};

struct ExecutionResult {
    Side side = Side::Buy;

    double requestedNotional = 0.0;
    double requestedBaseQty = 0.0;

    double filledNotional = 0.0;
    double filledBaseQty = 0.0;

    double referenceMid = 0.0;
    double bestPrice = 0.0;
    double executionVwap = 0.0;

    double spreadBps = 0.0;

    double slippageBps = 0.0;
    double slippageUsd = 0.0;

    double feeBps = 0.0;
    double feeUsd = 0.0;

    double totalCostBps = 0.0;
    double totalCostUsd = 0.0;

    std::size_t levelsConsumed = 0;
    bool insufficientLiquidity = false;
};

constexpr bool operator==(const ExecutionResult& lhs, const ExecutionResult& rhs) noexcept
{
    return lhs.side == rhs.side
        && lhs.requestedNotional == rhs.requestedNotional
        && lhs.requestedBaseQty == rhs.requestedBaseQty
        && lhs.filledNotional == rhs.filledNotional
        && lhs.filledBaseQty == rhs.filledBaseQty
        && lhs.referenceMid == rhs.referenceMid
        && lhs.bestPrice == rhs.bestPrice
        && lhs.executionVwap == rhs.executionVwap
        && lhs.spreadBps == rhs.spreadBps
        && lhs.slippageBps == rhs.slippageBps
        && lhs.slippageUsd == rhs.slippageUsd
        && lhs.feeBps == rhs.feeBps
        && lhs.feeUsd == rhs.feeUsd
        && lhs.totalCostBps == rhs.totalCostBps
        && lhs.totalCostUsd == rhs.totalCostUsd
        && lhs.levelsConsumed == rhs.levelsConsumed
        && lhs.insufficientLiquidity == rhs.insufficientLiquidity;
}

constexpr bool operator!=(const ExecutionResult& lhs, const ExecutionResult& rhs) noexcept
{
    return !(lhs == rhs);
}

} // namespace qx
