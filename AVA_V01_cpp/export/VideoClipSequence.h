#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

#include <optional>

/// One source file in the concatenation sequence. Parsing is filename-only:
/// `<concept>_<NNN>_<start>-<end>_pXX.ext`.
struct VideoClip {
    QString filePath;
    QString fileName;
    bool parsed = false;
    QString concept;
    /// Sequence number from the filename, without leading zeros ("003" → "3").
    QString sequenceLabel;
    bool hasStart = false;
    qint64 startMs = 0;
    bool hasEnd = false;
    qint64 endMs = 0;

    bool hasDuration() const;
    qint64 durationMs() const;
    /// "Penalty corner 3", or the file name when the pattern does not match.
    QString title() const;
    /// "10m55s–11m54s", or empty when either clock is missing.
    QString timeRangeText() const;
};

namespace VideoClipSequence {

VideoClip clipFromFilePath(const QString& filePath);
QVector<VideoClip> clipsFromFilePaths(const QStringList& filePaths);

/// Timed clips first, by start time. Untimed clips keep their relative order at the end.
QVector<VideoClip> chronologicalOrder(const QVector<VideoClip>& clips);
bool isChronological(const QVector<VideoClip>& clips);

/// Moves the clip at \p fromIndex so it lands at \p toIndex. Both indexes refer
/// to the list before the move. Returns false when either index is out of range.
bool moveClip(QVector<VideoClip>& clips, int fromIndex, int toIndex);

/// \p insertBefore is the gap in the pre-move list (0 through count). The result
/// is the \p toIndex to pass to moveClip, or -1 when \p fromIndex is unusable.
int finalIndexForDrop(int fromIndex, int insertBefore, int count);

/// Sum of clip durations when every clip has a known, non-negative span.
std::optional<qint64> totalDurationMs(const QVector<VideoClip>& clips);

QString compactClock(qint64 milliseconds);
QStringList filePathsOf(const QVector<VideoClip>& clips);

/// File name, or "folder/file name" when another clip shares the same name.
QString fileLabel(const QVector<VideoClip>& clips, int index);

/// Realistic filenames that stress parsing and row layout. Dev fixture and tests.
QStringList stressFilePaths();

}  // namespace VideoClipSequence
