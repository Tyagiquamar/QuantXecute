#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "qx/ExecutionResult.h"
#include "qx/Level.h"
#include "qx/MarketEvent.h"

TEST_CASE("core types default construct and compare")
{
    qx::Level level;
    CHECK(level == qx::Level{0.0, 0.0});
    CHECK_FALSE(level != qx::Level{0.0, 0.0});
    CHECK(level != qx::Level{1.0, 0.0});

    qx::MarketEvent event;
    CHECK(event.type == qx::EventType::Delta);
    CHECK(event.sequence == 0);
    CHECK(event.timestampNs == 0);
    CHECK_FALSE(event.checksum.has_value());
    CHECK(event.bids.empty());
    CHECK(event.asks.empty());

    qx::MarketEvent other;
    CHECK(event == other);

    qx::ExecutionResult result;
    CHECK(result.side == qx::Side::Buy);
    CHECK(result.levelsConsumed == 0);
    CHECK(result.totalCostBps == 0.0);
}
