#include "qx/Replay.h"

#include <chrono>
#include <thread>

#include "qx/Recorder.h"

namespace qx {

ReplayStats replayInto(Book& book, const std::string& logPath, const ReplayOptions& options)
{
    ReplayStats stats;
    EventLogReader reader(logPath);
    SequenceValidator validator(options.validatorMode);

    std::int64_t previousTimestampNs = 0;

    while (auto event = reader.next()) {
        if (options.speedMultiplier > 0.0 && previousTimestampNs != 0) {
            const std::int64_t gapNs = event->timestampNs - previousTimestampNs;
            if (gapNs > 0) {
                const double scaledNs = static_cast<double>(gapNs) / options.speedMultiplier;
                if (options.pace) {
                    options.pace(static_cast<std::int64_t>(scaledNs));
                } else {
                    std::this_thread::sleep_for(std::chrono::nanoseconds(
                        static_cast<std::int64_t>(scaledNs)));
                }
            }
        }
        previousTimestampNs = event->timestampNs;

        const auto verdict = validator.validate(*event);

        switch (verdict.verdict) {
        case SequenceValidator::Verdict::Accept:
            switch (event->type) {
            case EventType::Snapshot:
                book.applySnapshot(*event);
                break;
            case EventType::Delta:
                book.applyDelta(*event);
                break;
            }
            ++stats.eventsApplied;
            break;

        case SequenceValidator::Verdict::StaleReject:
            ++stats.staleRejected;
            break;

        case SequenceValidator::Verdict::GapResync:
            // Mirror the live pipeline: a continuity failure invalidates the
            // book until the next recorded snapshot re-establishes it.
            book.clear();
            validator.reset();
            ++stats.gapResyncs;
            break;
        }
    }

    stats.malformedLines = reader.malformedLines();
    stats.lastSequence = book.lastSequence();

    return stats;
}

} // namespace qx