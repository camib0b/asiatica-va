#pragma once

#include <QtGlobal>

/// Where an XML instance lands on the video timeline after the sync offset.
struct ImportedClipInterval {
    qint64 startMs = 0;
    qint64 endMs = 0;
    /// True when the instance began before the video's marked start.
    bool adjustedBeforeVideoStart = false;
};

/// Events that start before the marked video start are kept.
/// A clip that is entirely before the start keeps its original length at 00:00.
/// A clip that only starts early keeps the part that overlaps the video.
inline ImportedClipInterval clipIntervalOnVideoTimeline(qint64 instanceStartMs,
                                                        qint64 instanceEndMs,
                                                        qint64 offsetMs) {
    const qint64 rawStartMs = instanceStartMs + offsetMs;
    const qint64 rawEndMs = instanceEndMs + offsetMs;
    const qint64 originalLengthMs = qMax(qint64{0}, rawEndMs - rawStartMs);

    ImportedClipInterval interval;
    if (rawStartMs >= 0) {
        interval.startMs = rawStartMs;
        interval.endMs = qMax(rawStartMs, rawEndMs);
        return interval;
    }

    interval.adjustedBeforeVideoStart = true;
    interval.startMs = 0;
    if (rawEndMs <= 0) {
        interval.endMs = originalLengthMs;
        return interval;
    }
    interval.endMs = rawEndMs;
    return interval;
}
