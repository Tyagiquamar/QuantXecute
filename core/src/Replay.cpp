#include "qx/Replay.h"

#include <chrono>
#include <thread>

#include "qx/Recorder.h"

namespace qx {

ReplayStats replayInto(Book& book, const std::string& logPath, const ReplayOptions& options)
{
    ReplayStats stats;
    EventLogReader reader(logPath);

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

        switch (event->type) {
        case EventType::Snapshot:
            book.applySnapshot(*event);
            ++stats.eventsApplied;
            break;
        case EventType::Delta:
            if (book.applyDelta(*event) == ApplyStatus::Applied) {
                ++stats.eventsApplied;
            } else {
                ++stats.staleRejected;
            }
            break;
        }
    }

    stats.malformedLines = reader.malformedLines();
    stats.lastSequence = book.lastSequence();

    return stats;
}

} // namespace qx
