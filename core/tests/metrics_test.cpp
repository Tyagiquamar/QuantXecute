#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fixture_loader.h"
#include "qx/Book.h"
#include "qx/Metrics.h"
#include "qx/SequenceValidator.h"

TEST_CASE("histogram nearest-rank percentiles on known samples")
{
    qx::Histogram histogram;
    for (double value = 1.0; value <= 100.0; ++value) {
        histogram.observe(value);
    }

    CHECK(histogram.size() == 100);
    CHECK(histogram.percentile(0.0) == doctest::Approx(1.0));
    CHECK(histogram.percentile(50.0) == doctest::Approx(50.0));
    CHECK(histogram.percentile(90.0) == doctest::Approx(90.0));
    CHECK(histogram.percentile(95.0) == doctest::Approx(95.0));
    CHECK(histogram.percentile(99.0) == doctest::Approx(99.0));
    CHECK(histogram.percentile(100.0) == doctest::Approx(100.0));
    CHECK(histogram.mean() == doctest::Approx(50.5));
}

TEST_CASE("histogram handles empty and single-sample inputs")
{
    qx::Histogram empty;
    CHECK(empty.empty());
    CHECK(empty.percentile(50.0) == 0.0);
    CHECK(empty.mean() == 0.0);

    qx::Histogram single;
    single.observe(42.0);
    CHECK(single.percentile(50.0) == doctest::Approx(42.0));
    CHECK(single.percentile(1.0) == doctest::Approx(42.0));
}

TEST_CASE("percentiles summary matches individual queries")
{
    qx::Histogram histogram;
    const double values[] = {12.0, 4.0, 7.0, 30.0, 22.0, 18.0, 9.0, 44.0, 2.0};
    for (const double value : values) {
        histogram.observe(value);
    }

    const auto result = summarize(histogram);
    CHECK(result.p50 == histogram.percentile(50.0));
    CHECK(result.p95 == histogram.percentile(95.0));
    CHECK(result.p99 == histogram.percentile(99.0));
}

TEST_CASE("engine counters track exactly one increment per event")
{
    using qx::EventType;

    qx::SequenceValidator validator;
    qx::EngineCounters counters;
    qx::Book book;

    const auto events = qx::test::loadEventsJsonl(
        std::string(QX_FIXTURE_DIR) + "/btc_snapshot_deltas.jsonl");
    REQUIRE(events.size() == 4);

    for (const auto& raw : events) {
        ++counters.framesReceived;

        const auto verdict = validator.validate(raw);
        switch (verdict.verdict) {
        case qx::SequenceValidator::Verdict::Accept: {
            qx::MarketEvent event = raw;
            event.sequence = verdict.effectiveSequence;
            if (event.type == EventType::Snapshot) {
                book.applySnapshot(event);
                ++counters.snapshotsApplied;
            } else {
                (void)book.applyDelta(event);
                ++counters.deltasApplied;
            }
            if (validator.verifyChecksum(book, event, qx::IntegrityPolicy::SequenceOnly)
                    == qx::SequenceValidator::ChecksumVerdict::Mismatch) {
                ++counters.checksumFailures;
            }
            break;
        }
        case qx::SequenceValidator::Verdict::StaleReject:
            ++counters.staleRejected;
            break;
        case qx::SequenceValidator::Verdict::GapResync:
            ++counters.gapsDetected;
            book.clear();
            break;
        }
    }

    CHECK(counters.framesReceived == 4);
    CHECK(counters.snapshotsApplied == 1);
    CHECK(counters.deltasApplied == 3);
    CHECK(counters.staleRejected == 0);
    CHECK(counters.gapsDetected == 0);
    CHECK(counters.malformedFrames == 0);
}
