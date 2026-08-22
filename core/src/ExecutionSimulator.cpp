#include "qx/ExecutionSimulator.h"

#include <cmath>

namespace qx {

namespace {

constexpr double kRelativeEpsilon = 1e-9;

bool remainingIsNegligible(double remaining, double reference)
{
    return remaining <= kRelativeEpsilon * reference;
}

void accumulateDepth(const std::vector<Level>& levels, Side side, double mid,
    std::array<double, kDepthBandCount>& out)
{
    for (std::size_t i = 0; i < kDepthBandCount; ++i) {
        const double band = mid * kDepthBandsBps[i] / 10000.0;
        double total = 0.0;
        for (const auto& level : levels) {
            const bool inBand = side == Side::Buy
                ? level.price <= mid + band + kRelativeEpsilon * band
                : level.price >= mid - band - kRelativeEpsilon * band;
            if (inBand && level.price > 0.0) {
                total += level.price * level.size;
            }
        }
        out[i] = total;
    }
}

} // namespace

ExecutionResult ExecutionSimulator::execute(const Book& book, const OrderRequest& request) const
{
    ExecutionResult out;
    out.side = request.side;
    out.feeBps = request.takerFeeBps;

    if (request.sizeMode == SizeMode::Notional) {
        out.requestedNotional = request.size;
    } else {
        out.requestedBaseQty = request.size;
    }

    if (!(request.size > 0.0)) {
        out.insufficientLiquidity = true;
        return out;
    }

    const std::vector<Level> ladder = request.side == Side::Buy ? book.asks() : book.bids();
    if (ladder.empty()) {
        out.insufficientLiquidity = true;
        return out;
    }

    out.bestPrice = ladder.front().price;
    out.spreadBps = book.spreadBps().value_or(0.0);
    if (const auto mid = book.mid()) {
        out.referenceMid = *mid;
    } else {
        out.referenceMid = ladder.front().price;
    }

    double remainingBase = 0.0;
    double remainingNotional = 0.0;
    if (request.sizeMode == SizeMode::Notional) {
        remainingNotional = request.size;
    } else {
        remainingBase = request.size;
    }

    for (const auto& level : ladder) {
        if (level.price <= 0.0 || level.size <= 0.0) {
            continue;
        }

        double take;
        if (request.sizeMode == SizeMode::Notional) {
            if (remainingIsNegligible(remainingNotional, request.size)) {
                break;
            }
            take = std::min(level.size, remainingNotional / level.price);
            remainingNotional -= take * level.price;
        } else {
            if (remainingIsNegligible(remainingBase, request.size)) {
                break;
            }
            take = std::min(level.size, remainingBase);
            remainingBase -= take;
        }

        out.filledBaseQty += take;
        out.filledNotional += take * level.price;
        ++out.levelsConsumed;
    }

    if (out.filledBaseQty <= 0.0 || out.filledNotional <= 0.0) {
        out.insufficientLiquidity = true;
        return out;
    }

    if (request.sizeMode == SizeMode::Notional
        && !remainingIsNegligible(remainingNotional, request.size)) {
        out.insufficientLiquidity = true;
    }
    if (request.sizeMode == SizeMode::BaseQuantity
        && !remainingIsNegligible(remainingBase, request.size)) {
        out.insufficientLiquidity = true;
    }

    out.executionVwap = out.filledNotional / out.filledBaseQty;

    const double idealNotional = out.filledBaseQty * out.referenceMid;
    out.slippageUsd = std::abs(out.filledNotional - idealNotional);
    out.slippageBps = out.slippageUsd / idealNotional * 10000.0;

    out.feeUsd = out.filledNotional * request.takerFeeBps / 10000.0;
    out.totalCostUsd = out.slippageUsd + out.feeUsd;
    out.totalCostBps = out.slippageBps + request.takerFeeBps;

    if (const auto mid = book.mid()) {
        accumulateDepth(book.asks(), Side::Buy, *mid, out.askDepthNotionalWithinBps);
        accumulateDepth(book.bids(), Side::Sell, *mid, out.bidDepthNotionalWithinBps);
    }

    return out;
}

} // namespace qx
