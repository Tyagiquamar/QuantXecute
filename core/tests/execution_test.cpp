#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "qx/ExecutionSimulator.h"

namespace {

qx::Level level(double price, double size)
{
    return qx::Level { price, size };
}

qx::MarketEvent snapshotEvent(std::uint64_t seq,
    std::vector<qx::Level> bids, std::vector<qx::Level> asks)
{
    qx::MarketEvent event;
    event.type = qx::EventType::Snapshot;
    event.sequence = seq;
    event.bids = std::move(bids);
    event.asks = std::move(asks);
    return event;
}

} // namespace

TEST_CASE("golden worked example: BUY 1000 base against two-level ask ladder")
{
    qx::Book book;
    book.applySnapshot(snapshotEvent(1,
        { level(95.0, 1000.0) },
        { level(100.0, 500.0), level(110.0, 500.0) }));

    const qx::OrderRequest request {
        qx::Side::Buy, qx::SizeMode::BaseQuantity, 1000.0, 5.0 };
    const qx::ExecutionResult result = qx::ExecutionSimulator {}.execute(book, request);

    CHECK_FALSE(result.insufficientLiquidity);
    CHECK(result.requestedBaseQty == 1000.0);
    CHECK(result.filledBaseQty == 1000.0);
    CHECK(result.filledNotional == doctest::Approx(105000.0));
    CHECK(result.bestPrice == 100.0);
    CHECK(result.referenceMid == doctest::Approx(97.5));
    CHECK(result.executionVwap == doctest::Approx(105.0));
    CHECK(result.levelsConsumed == 2);
    CHECK(result.spreadBps == doctest::Approx((5.0 / 97.5) * 10000.0));

    CHECK(result.slippageUsd == doctest::Approx(7500.0));
    CHECK(result.slippageBps == doctest::Approx((7500.0 / 97500.0) * 10000.0));

    CHECK(result.feeBps == 5.0);
    CHECK(result.feeUsd == doctest::Approx(52.5));
    CHECK(result.totalCostUsd == doctest::Approx(7552.5));
    CHECK(result.totalCostBps == doctest::Approx(result.slippageBps + 5.0));
}

TEST_CASE("SELL walks bids descending with symmetric accounting")
{
    qx::Book book;
    book.applySnapshot(snapshotEvent(1,
        { level(100.0, 500.0), level(90.0, 500.0) },
        { level(105.0, 100.0) }));

    const qx::OrderRequest request {
        qx::Side::Sell, qx::SizeMode::BaseQuantity, 1000.0, 5.0 };
    const qx::ExecutionResult result = qx::ExecutionSimulator {}.execute(book, request);

    CHECK_FALSE(result.insufficientLiquidity);
    CHECK(result.filledBaseQty == 1000.0);
    CHECK(result.filledNotional == doctest::Approx(95000.0));
    CHECK(result.bestPrice == 100.0);
    CHECK(result.referenceMid == doctest::Approx(102.5));
    CHECK(result.executionVwap == doctest::Approx(95.0));
    CHECK(result.levelsConsumed == 2);
    CHECK(result.slippageUsd == doctest::Approx(7500.0));
    CHECK(result.slippageBps == doctest::Approx((7500.0 / 102500.0) * 10000.0));
    CHECK(result.feeUsd == doctest::Approx(47.5));
}

TEST_CASE("notional-sized BUY consumes the ladder proportionally")
{
    qx::Book book;
    book.applySnapshot(snapshotEvent(1,
        { level(95.0, 1000.0) },
        { level(100.0, 500.0), level(110.0, 500.0) }));

    const qx::OrderRequest request {
        qx::Side::Buy, qx::SizeMode::Notional, 70000.0, 5.0 };
    const qx::ExecutionResult result = qx::ExecutionSimulator {}.execute(book, request);

    CHECK_FALSE(result.insufficientLiquidity);
    CHECK(result.requestedNotional == 70000.0);
    CHECK(result.requestedBaseQty == 0.0);
    CHECK(result.filledBaseQty == doctest::Approx(500.0 + 20000.0 / 110.0));
    CHECK(result.filledNotional == doctest::Approx(70000.0));
    CHECK(result.levelsConsumed == 2);
}

TEST_CASE("exact-fill consumes full depth without flagging insufficiency")
{
    qx::Book book;
    book.applySnapshot(snapshotEvent(1,
        { level(95.0, 1000.0) },
        { level(100.0, 500.0), level(110.0, 500.0) }));

    const qx::ExecutionResult result = qx::ExecutionSimulator {}.execute(book,
        { qx::Side::Buy, qx::SizeMode::Notional, 105000.0, 0.0 });

    CHECK_FALSE(result.insufficientLiquidity);
    CHECK(result.filledNotional == doctest::Approx(105000.0));
    CHECK(result.filledBaseQty == doctest::Approx(1000.0));
}

TEST_CASE("partial fill flags insufficient liquidity without divide-by-zero")
{
    qx::Book book;
    book.applySnapshot(snapshotEvent(1,
        { level(95.0, 1000.0) },
        { level(100.0, 500.0), level(110.0, 500.0) }));

    const qx::ExecutionResult result = qx::ExecutionSimulator {}.execute(book,
        { qx::Side::Buy, qx::SizeMode::Notional, 200000.0, 0.0 });

    CHECK(result.insufficientLiquidity);
    CHECK(result.filledNotional == doctest::Approx(105000.0));
    CHECK(result.filledNotional < result.requestedNotional);
    CHECK(result.filledBaseQty == doctest::Approx(1000.0));
    CHECK(result.executionVwap == doctest::Approx(105.0));
}

TEST_CASE("single-level fill matches best price with exact fee math")
{
    qx::Book book;
    book.applySnapshot(snapshotEvent(1,
        { level(98.0, 300.0) },
        { level(100.0, 300.0) }));

    const qx::ExecutionResult result = qx::ExecutionSimulator {}.execute(book,
        { qx::Side::Buy, qx::SizeMode::BaseQuantity, 300.0, 5.0 });

    CHECK_FALSE(result.insufficientLiquidity);
    CHECK(result.executionVwap == result.bestPrice);
    CHECK(result.levelsConsumed == 1);
    CHECK(result.slippageUsd == doctest::Approx(300.0));
    CHECK(result.slippageBps == doctest::Approx((300.0 / 29700.0) * 10000.0));
    CHECK(result.feeUsd == doctest::Approx(15.0));
    CHECK(result.totalCostUsd == doctest::Approx(315.0));
}

TEST_CASE("depth within bps bands respects a known ladder gap")
{
    qx::Book book;
    book.applySnapshot(snapshotEvent(1,
        { level(99.98, 400.0), level(99.60, 250.0) },
        { level(100.02, 200.0), level(100.40, 300.0) }));

    const qx::ExecutionResult result = qx::ExecutionSimulator {}.execute(book,
        { qx::Side::Buy, qx::SizeMode::BaseQuantity, 10.0, 0.0 });

    CHECK(book.mid().value() == doctest::Approx(100.0));

    REQUIRE(result.askDepthNotionalWithinBps.size() == 4);
    CHECK(result.askDepthNotionalWithinBps[0] == doctest::Approx(20004.0));
    CHECK(result.askDepthNotionalWithinBps[1] == doctest::Approx(20004.0));
    CHECK(result.askDepthNotionalWithinBps[2] == doctest::Approx(20004.0));
    CHECK(result.askDepthNotionalWithinBps[3] == doctest::Approx(50124.0));

    CHECK(result.bidDepthNotionalWithinBps[0] == doctest::Approx(39992.0));
    CHECK(result.bidDepthNotionalWithinBps[1] == doctest::Approx(39992.0));
    CHECK(result.bidDepthNotionalWithinBps[2] == doctest::Approx(39992.0));
    CHECK(result.bidDepthNotionalWithinBps[3] == doctest::Approx(64892.0));
}

TEST_CASE("empty or invalid requests report insufficient liquidity safely")
{
    SUBCASE("empty book")
    {
        qx::Book book;
        const qx::ExecutionResult result = qx::ExecutionSimulator {}.execute(book,
            { qx::Side::Buy, qx::SizeMode::Notional, 1000.0, 5.0 });
        CHECK(result.insufficientLiquidity);
        CHECK(result.filledBaseQty == 0.0);
        CHECK(result.executionVwap == 0.0);
    }

    SUBCASE("zero-size order")
    {
        qx::Book book;
        book.applySnapshot(snapshotEvent(1,
            { level(99.0, 1.0) }, { level(101.0, 1.0) }));
        const qx::ExecutionResult result = qx::ExecutionSimulator {}.execute(book,
            { qx::Side::Buy, qx::SizeMode::Notional, 0.0, 5.0 });
        CHECK(result.insufficientLiquidity);
    }

    SUBCASE("one-sided book falls back to consumed-side reference")
    {
        qx::Book book;
        book.applySnapshot(snapshotEvent(1,
            {}, { level(100.0, 500.0) }));
        const qx::ExecutionResult result = qx::ExecutionSimulator {}.execute(book,
            { qx::Side::Buy, qx::SizeMode::BaseQuantity, 100.0, 5.0 });
        CHECK_FALSE(result.insufficientLiquidity);
        CHECK(result.referenceMid == 100.0);
        CHECK(result.spreadBps == 0.0);
        CHECK(result.executionVwap == doctest::Approx(100.0));
        CHECK(result.slippageBps == doctest::Approx(0.0));
    }
}
