#include "VideoClipSequence.h"

#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <array>
#include <limits>

namespace {

bool isVideoExtension(const QString& suffix) {
    const QString lower = suffix.toLower();
    constexpr std::array<QLatin1String, 10> extensions = {
        QLatin1String("mp4"), QLatin1String("mov"), QLatin1String("m4v"),
        QLatin1String("mkv"), QLatin1String("avi"), QLatin1String("webm"),
        QLatin1String("mpg"), QLatin1String("mpeg"), QLatin1String("wmv"),
        QLatin1String("qt"),
    };
    return std::find(extensions.cbegin(), extensions.cend(), lower) != extensions.cend();
}

QString oneLine(const QString& text) {
    QString cleaned = text;
    for (int index = 0; index < cleaned.size(); ++index) {
        const QChar character = cleaned.at(index);
        if (character == QLatin1Char('\n') || character == QLatin1Char('\r') ||
            character == QChar(0x2028) || character == QChar(0x2029) ||
            character == QLatin1Char('\t')) {
            cleaned[index] = QLatin1Char(' ');
        }
    }
    return cleaned;
}

QString sentenceCaseWords(const QString& rawConcept) {
    QString text = rawConcept;
    text.replace(QLatin1Char('_'), QLatin1Char(' '));
    text = text.simplified();
    if (text.isEmpty()) return {};
    text = text.toLower();
    text[0] = text.at(0).toUpper();
    return text;
}

QString normalizeSequenceLabel(const QString& digits) {
    bool ok = false;
    const qint64 value = digits.toLongLong(&ok);
    if (!ok || value < 0) return digits;
    return QString::number(value);
}

bool parseClockToken(const QString& token, qint64* milliseconds) {
    static const QRegularExpression clockPattern(
        QStringLiteral(R"(^(?:(\d+)h)?(?:(\d+)m)?(?:(\d+)s)?$)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = clockPattern.match(token);
    if (!match.hasMatch()) return false;

    const bool hasHours = !match.captured(1).isEmpty();
    const bool hasMinutes = !match.captured(2).isEmpty();
    const bool hasSeconds = !match.captured(3).isEmpty();
    if (!hasHours && !hasMinutes && !hasSeconds) return false;

    auto component = [&match](int group) -> std::optional<qint64> {
        const QString text = match.captured(group);
        if (text.isEmpty()) return 0;
        bool ok = false;
        const qint64 value = text.toLongLong(&ok);
        if (!ok || value < 0) return std::nullopt;
        return value;
    };

    const std::optional<qint64> hours = component(1);
    const std::optional<qint64> minutes = component(2);
    const std::optional<qint64> seconds = component(3);
    if (!hours.has_value() || !minutes.has_value() || !seconds.has_value()) return false;

    constexpr qint64 kMax = std::numeric_limits<qint64>::max();
    if (*hours > kMax / 3600000) return false;
    qint64 total = *hours * 3600000;
    if (*minutes > kMax / 60000) return false;
    if (total > kMax - (*minutes * 60000)) return false;
    total += *minutes * 60000;
    if (*seconds > kMax / 1000) return false;
    if (total > kMax - (*seconds * 1000)) return false;
    total += *seconds * 1000;
    *milliseconds = total;
    return true;
}

QString patternStem(const QString& fileName) {
    const QFileInfo info(fileName);
    if (!isVideoExtension(info.suffix())) return fileName;
    return info.completeBaseName();
}

}  // namespace

bool VideoClip::hasDuration() const {
    return hasStart && hasEnd && endMs >= startMs;
}

qint64 VideoClip::durationMs() const {
    if (!hasDuration()) return 0;
    return endMs - startMs;
}

QString VideoClip::title() const {
    const QString fallback = oneLine(fileName.isEmpty() ? filePath : fileName);
    if (!parsed) return fallback;
    const QString conceptLabel = sentenceCaseWords(concept);
    if (conceptLabel.isEmpty()) return fallback;
    if (sequenceLabel.isEmpty()) return conceptLabel;
    return conceptLabel + QLatin1Char(' ') + sequenceLabel;
}

QString VideoClip::timeRangeText() const {
    if (!hasStart || !hasEnd) return {};
    return VideoClipSequence::compactClock(startMs) + QStringLiteral("\u2013") +
           VideoClipSequence::compactClock(endMs);
}

namespace VideoClipSequence {

VideoClip clipFromFilePath(const QString& filePath) {
    VideoClip clip;
    clip.filePath = filePath;
    const QString fileName = QFileInfo(filePath).fileName();
    clip.fileName = fileName.isEmpty() ? filePath : fileName;

    static const QRegularExpression namePattern(
        QStringLiteral(R"(^(.+)_(\d+)_([0-9hms]+)-([0-9hms]+)_p(\d+)$)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = namePattern.match(patternStem(clip.fileName));
    if (!match.hasMatch()) return clip;

    qint64 startMs = 0;
    qint64 endMs = 0;
    if (!parseClockToken(match.captured(3), &startMs) || !parseClockToken(match.captured(4), &endMs)) {
        return clip;
    }

    clip.parsed = true;
    clip.concept = match.captured(1);
    clip.sequenceLabel = normalizeSequenceLabel(match.captured(2));
    clip.hasStart = true;
    clip.startMs = startMs;
    clip.hasEnd = true;
    clip.endMs = endMs;
    return clip;
}

QVector<VideoClip> clipsFromFilePaths(const QStringList& filePaths) {
    QVector<VideoClip> clips;
    clips.reserve(filePaths.size());
    for (const QString& filePath : filePaths) {
        clips.append(clipFromFilePath(filePath));
    }
    return clips;
}

QVector<VideoClip> chronologicalOrder(const QVector<VideoClip>& clips) {
    QVector<VideoClip> ordered = clips;
    std::stable_sort(ordered.begin(), ordered.end(), [](const VideoClip& left, const VideoClip& right) {
        if (left.hasStart != right.hasStart) return left.hasStart && !right.hasStart;
        if (left.hasStart && right.hasStart && left.startMs != right.startMs) {
            return left.startMs < right.startMs;
        }
        return false;
    });
    return ordered;
}

bool isChronological(const QVector<VideoClip>& clips) {
    const QVector<VideoClip> ordered = chronologicalOrder(clips);
    if (ordered.size() != clips.size()) return false;
    for (int index = 0; index < clips.size(); ++index) {
        if (ordered.at(index).filePath != clips.at(index).filePath) return false;
    }
    return true;
}

bool moveClip(QVector<VideoClip>& clips, int fromIndex, int toIndex) {
    if (fromIndex < 0 || toIndex < 0 || fromIndex >= clips.size() || toIndex >= clips.size()) {
        return false;
    }
    if (fromIndex == toIndex) return true;
    const VideoClip clip = clips.takeAt(fromIndex);
    clips.insert(toIndex, clip);
    return true;
}

int finalIndexForDrop(int fromIndex, int insertBefore, int count) {
    if (count <= 0 || fromIndex < 0 || fromIndex >= count) return -1;
    if (insertBefore < 0) insertBefore = 0;
    if (insertBefore > count) insertBefore = count;
    int target = insertBefore;
    if (fromIndex < insertBefore) target -= 1;
    if (target < 0) target = 0;
    if (target >= count) target = count - 1;
    return target;
}

std::optional<qint64> totalDurationMs(const QVector<VideoClip>& clips) {
    if (clips.isEmpty()) return std::nullopt;
    qint64 total = 0;
    for (const VideoClip& clip : clips) {
        if (!clip.hasDuration()) return std::nullopt;
        const qint64 duration = clip.durationMs();
        if (total > std::numeric_limits<qint64>::max() - duration) return std::nullopt;
        total += duration;
    }
    return total;
}

QString compactClock(qint64 milliseconds) {
    if (milliseconds < 0) return {};
    const qint64 totalSeconds = milliseconds / 1000;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    if (hours > 0) {
        return QStringLiteral("%1h%2m%3s")
            .arg(hours)
            .arg(minutes, 2, 10, QChar(u'0'))
            .arg(seconds, 2, 10, QChar(u'0'));
    }
    if (minutes > 0) {
        return QStringLiteral("%1m%2s").arg(minutes).arg(seconds, 2, 10, QChar(u'0'));
    }
    return QStringLiteral("%1s").arg(seconds);
}

QStringList filePathsOf(const QVector<VideoClip>& clips) {
    QStringList filePaths;
    filePaths.reserve(clips.size());
    for (const VideoClip& clip : clips) {
        filePaths.append(clip.filePath);
    }
    return filePaths;
}

QString fileLabel(const QVector<VideoClip>& clips, int index) {
    if (index < 0 || index >= clips.size()) return {};
    const VideoClip& clip = clips.at(index);
    const QString visibleName = oneLine(clip.fileName);
    bool sharedName = false;
    for (int other = 0; other < clips.size(); ++other) {
        if (other == index) continue;
        if (clips.at(other).fileName == clip.fileName) {
            sharedName = true;
            break;
        }
    }
    if (!sharedName) return visibleName;
    const QString folder = QFileInfo(clip.filePath).dir().dirName();
    if (folder.isEmpty() || folder == QLatin1String(".") || folder == QLatin1String("..")) {
        return visibleName;
    }
    return oneLine(folder) + QLatin1Char('/') + visibleName;
}

QStringList stressFilePaths() {
    return {
        QStringLiteral("/matches/inbox/bartholomew.fitzgerald@northwind-industries-holdings.example.com.mp4"),
        QString::fromUtf8("/matches/long/aleksandra_wiśniewska-kowalczyk_categoría_sub_17_010_4m00s-5m15s_p72.mp4"),
        QStringLiteral("/matches/short/a_001_0m01s-0m02s_p1.mp4"),
        QStringLiteral("/matches/markup/<b>bold</b>_012_8m00s-9m00s_p3.mp4"),
        QStringLiteral("/matches/camera/IMG_20250914_183022_HDR_portrait_edited_edited.HEIC"),
        QString::fromUtf8("/matches/vn/đặng_thị_ngọc_hân_008_0m30s-1m00s_p4.mp4"),
        QStringLiteral("/matches/bad/clip_013_12m00s-11m00s_p1.mp4"),
        QStringLiteral("/matches/cam-b/penalty_corner_004_2m00s-3m30s_p63.mp4"),
        QStringLiteral("/matches/cam-a/penalty_corner_004_2m00s-3m30s_p63.mp4"),
        QString::fromUtf8("/matches/emoji/🦊_fox_005_1m00s-1m00s_p9.mp4"),
        QStringLiteral("/matches/deck/Q3 Board Deck — FINAL (revised) v12 [approved by legal].mp4"),
        QString::fromUtf8("/matches/odd/line_one\nline_two.mp4"),
        QString::fromUtf8("/matches/rtl/نور_الهدى_011_6m00s-7m00s_p2.mp4"),
        QStringLiteral("/matches/hours/archivo_015_1h05m00s-1h08m30s_p1.mp4"),
        QStringLiteral("/matches/script/<script>alert(1)</script>_016_30m00s-31m00s_p7.mp4"),
        QStringLiteral("/matches/blank/untitled.mp4"),
    };
}

}  // namespace VideoClipSequence
