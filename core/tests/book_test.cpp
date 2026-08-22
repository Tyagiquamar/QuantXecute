#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "fixture_loader.h"
#include "qx/Book.h"

namespace {

std::vector<qx::Level> levels(std::initializer_list<std::pair<double, double>> init)
{
    std::vector<qx::Level> out;
    out.reserve(init.size());
    for (const auto& [price, size] : init) {
        out.push_back(qx::Level { price, size });
    }
    return out;
}

} // namespace

TEST_CASE("snapshot resets state and establishes sequence")
{
    qx::Book book;

    qx::MarketEvent snap;
    snap.type = qx::EventType::Snapshot;
    snap.sequence = 10;
    snap.bids = levels({ { 100.0, 1.0 } });
    snap.asks = levels({ { 101.0, 2.0 }, { 102.0, 3.0 } });
    book.applySnapshot(snap);

    CHECK(book.lastSequence() == 10);
    CHECK_FALSE(book.empty());
    CHECK(book.bids().size() == 1);
    CHECK(book.asks().size() == 2);

    qx::MarketEvent snap2;
    snap2.type = qx::EventType::Snapshot;
    snap2.sequence = 20;
    snap2.bids = levels({ { 50.0, 1.0 } });
    snap2.asks = levels({ { 51.0, 1.0 } });
    book.applySnapshot(snap2);

    CHECK(book.lastSequence() == 20);
    CHECK(book.bids().size() == 1);
    CHECK(book.asks().size() == 1);
    REQUIRE(book.bestBid().has_value());
    CHECK(book.bestBid()->price == 50.0);
}

TEST_CASE("deltas insert update and delete price levels")
{
    qx::Book book;

    qx::MarketEvent snap;
    snap.type = qx::EventType::Snapshot;
    snap.sequence = 1;
    snap.bids = levels({ { 63000.5, 1.20 }, { 62999.0, 0.75 } });
    snap.asks = levels({ { 63001.0, 0.90 } });
    book.applySnapshot(snap);

    SUBCASE("insert new level")
    {
        qx::MarketEvent delta;
        delta.sequence = 2;
        delta.bids = levels({ { 63000.9, 0.35 } });
        CHECK(book.applyDelta(delta) == qx::ApplyStatus::Applied);

        const auto bids = book.bids();
        REQUIRE(bids.size() == 3);
        CHECK(bids[0].price == 63000.9);
        CHECK(bids[0].size == 0.35);
        CHECK(bids[1].price == 63000.5);
        CHECK(bids[2].price == 62999.0);
    }

    SUBCASE("update existing level size")
    {
        qx::MarketEvent delta;
        delta.sequence = 2;
        delta.bids = levels({ { 62999.0, 0.85 } });
        CHECK(book.applyDelta(delta) == qx::ApplyStatus::Applied);

        const auto bids = book.bids();
        REQUIRE(bids.size() == 2);
        CHECK(bids[1].price == 62999.0);
        CHECK(bids[1].size == 0.85);
    }

    SUBCASE("delete via zero size")
    {
        qx::MarketEvent delta;
        delta.sequence = 2;
        delta.asks = levels({ { 63001.0, 0.0 } });
        CHECK(book.applyDelta(delta) == qx::ApplyStatus::Applied);

        CHECK(book.asks().empty());
        CHECK_FALSE(book.bestAsk().has_value());
    }
}

TEST_CASE("ordering is strict after arbitrary insertion order")
{
    qx::Book book;

    qx::MarketEvent snap;
    snap.type = qx::EventType::Snapshot;
    snap.sequence = 7;
    snap.bids = levels({ { 300.0, 1.0 }, { 100.0, 1.0 }, { 400.0, 1.0 }, { 200.0, 1.0 } });
    snap.asks = levels({ { 550.0, 1.0 }, { 450.0, 1.0 }, { 650.0, 1.0 } });
    book.applySnapshot(snap);

    const auto bids = book.bids();
    const auto asks = book.asks();

    REQUIRE(bids.size() == 4);
    for (std::size_t i = 1; i < bids.size(); ++i) {
        CHECK(bids[i - 1].price > bids[i].price);
    }
    CHECK(bids.front().price == 400.0);
    CHECK(bids.back().price == 100.0);

    REQUIRE(asks.size() == 3);
    for (std::size_t i = 1; i < asks.size(); ++i) {
        CHECK(asks[i - 1].price < asks[i].price);
    }
    CHECK(asks.front().price == 450.0);
    CHECK(asks.back().price == 650.0);
}

TEST_CASE("deleting a non-existent level is a no-op")
{
    qx::Book book;

    qx::MarketEvent snap;
    snap.type = qx::EventType::Snapshot;
    snap.sequence = 1;
    snap.bids = levels({ { 100.0, 1.0 } });
    snap.asks = levels({ { 101.0, 1.0 } });
    book.applySnapshot(snap);

    qx::MarketEvent delta;
    delta.sequence = 2;
    delta.bids = levels({ { 42.0, 0.0 } });
    delta.asks = levels({ { 43.0, 0.0 } });
    CHECK(book.applyDelta(delta) == qx::ApplyStatus::Applied);

    CHECK(book.bids().size() == 1);
    CHECK(book.asks().size() == 1);
    CHECK(book.lastSequence() == 2);
}

TEST_CASE("stale and gapped deltas are rejected without state change")
{
    qx::Book book;

    qx::MarketEvent snap;
    snap.type = qx::EventType::Snapshot;
    snap.sequence = 10;
    snap.bids = levels({ { 100.0, 1.0 } });
    snap.asks = levels({ { 101.0, 1.0 } });
    book.applySnapshot(snap);

    qx::MarketEvent stale;
    stale.sequence = 10;
    stale.bids = levels({ { 99.0, 5.0 } });
    CHECK(book.applyDelta(stale) == qx::ApplyStatus::StaleRejected);

    qx::MarketEvent older;
    older.sequence = 4;
    older.bids = levels({ { 98.0, 5.0 } });
    CHECK(book.applyDelta(older) == qx::ApplyStatus::StaleRejected);

    qx::MarketEvent gap;
    gap.sequence = 13;
    gap.bids = levels({ { 97.0, 5.0 } });
    CHECK(book.applyDelta(gap) == qx::ApplyStatus::GapRejected);

    CHECK(book.lastSequence() == 10);
    REQUIRE(book.bestBid().has_value());
    CHECK(book.bestBid()->price == 100.0);
}

TEST_CASE("crossed book detection")
{
    qx::Book book;

    qx::MarketEvent healthySnap;
    healthySnap.type = qx::EventType::Snapshot;
    healthySnap.sequence = 1;
    healthySnap.bids = levels({ { 100.0, 1.0 } });
    healthySnap.asks = levels({ { 101.0, 1.0 } });
    book.applySnapshot(healthySnap);
    CHECK_FALSE(book.isCrossed());

    qx::MarketEvent crossDelta;
    crossDelta.sequence = 2;
    crossDelta.bids = levels({ { 101.25, 1.0 } });
    CHECK(book.applyDelta(crossDelta) == qx::ApplyStatus::Applied);
    CHECK(book.isCrossed());

    SUBCASE("fixture reproduces crossed book")
    {
        qx::Book fixtureBook;
        for (const auto& event : qx::test::loadEventsJsonl(std::string(QX_FIXTURE_DIR) + "/crossed_book.jsonl")) {
            if (event.type == qx::EventType::Snapshot) {
                fixtureBook.applySnapshot(event);
            } else {
                CHECK(fixtureBook.applyDelta(event) == qx::ApplyStatus::Applied);
            }
        }
        CHECK(fixtureBook.isCrossed());
        CHECK(fixtureBook.lastSequence() == 2001);
    }
}

TEST_CASE("empty book reports unavailable sentinels without UB")
{
    qx::Book book;

    CHECK(book.empty());
    CHECK_FALSE(book.bestBid().has_value());
    CHECK_FALSE(book.bestAsk().has_value());
    CHECK_FALSE(book.mid().has_value());
    CHECK_FALSE(book.spreadBps().has_value());
    CHECK_FALSE(book.isCrossed());
    CHECK(book.lastSequence() == 0);
    CHECK(book.bids().empty());
    CHECK(book.asks().empty());

    qx::MarketEvent bidOnly;
    bidOnly.type = qx::EventType::Snapshot;
    bidOnly.sequence = 1;
    bidOnly.bids = levels({ { 100.0, 1.0 } });
    book.applySnapshot(bidOnly);

    CHECK(book.bestBid().has_value());
    CHECK_FALSE(book.mid().has_value());
    CHECK_FALSE(book.spreadBps().has_value());
    CHECK_FALSE(book.isCrossed());
}

TEST_CASE("mid and spread bps computed from best levels")
{
    qx::Book book;

    qx::MarketEvent snap;
    snap.type = qx::EventType::Snapshot;
    snap.sequence = 1;
    snap.bids = levels({ { 100.0, 1.0 } });
    snap.asks = levels({ { 100.2, 1.0 } });
    book.applySnapshot(snap);

    REQUIRE(book.mid().has_value());
    CHECK(*book.mid() == doctest::Approx(100.1));

    REQUIRE(book.spreadBps().has_value());
    CHECK(*book.spreadBps() == doctest::Approx((0.2 / 100.1) * 10000.0).epsilon(0.001));
}

TEST_CASE("serialize is identical for identically-built books")
{
    const auto applyEvents = [](qx::Book& book) {
        for (const auto& event : qx::test::loadEventsJsonl(std::string(QX_FIXTURE_DIR) + "/btc_snapshot_deltas.jsonl")) {
            if (event.type == qx::EventType::Snapshot) {
                book.applySnapshot(event);
            } else {
                book.applyDelta(event);
            }
        }
    };

    qx::Book first;
    applyEvents(first);

    qx::Book second;
    applyEvents(second);

    CHECK(first.serialize() == second.serialize());
    CHECK_FALSE(first.serialize().empty());

    const auto bids = first.bids();
    const auto asks = first.asks();

    REQUIRE(bids.size() == 3);
    CHECK(bids[0].price == 63000.9);
    CHECK(bids[0].size == 0.35);
    CHECK(bids[1].price == 63000.5);
    CHECK(bids[1].size == 1.20);
    CHECK(bids[2].price == 62999.0);
    CHECK(bids[2].size == 0.85);

    REQUIRE(asks.size() == 3);
    CHECK(asks[0].price == 63001.0);
    CHECK(asks[0].size == 0.90);
    CHECK(asks[1].price == 63004.0);
    CHECK(asks[1].size == 0.60);
    CHECK(asks[2].price == 63005.5);
    CHECK(asks[2].size == 0.70);
}
