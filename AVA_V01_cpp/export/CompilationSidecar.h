#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

#include <optional>

/// Identity of one compiled clip, stored on the same ClipSegment list that is
/// encoded and concatenated. Ordinals and offsets follow this list's order.
struct ClipSidecarLabel {
    QString group;
    QString text;
};

struct ClipCompilationRecord {
    /// Team abbreviation used in the XML code ("OR"). Empty when the session has none.
    QString teamAbbrev;
    /// XML <code> of the tagged-team instance ("OR 16YD+"). Empty when none was emitted.
    QString code;
    /// Stable prefix burned on every frame ("OR - 16-yd"), without the "n / N" counter.
    /// The burned line is "{overlayLabel}  {ord} / {N}".
    QString overlayLabel;
    /// QUARTOS text used for chapter titles ("Q1"). Empty when the tag has no period.
    QString periodLabel;
    /// Full-match in/out point in milliseconds, matching the XML <start>/<end> when
    /// this clip has an instance. This is not the padded encode length.
    qint64 sourceStartMs = 0;
    qint64 sourceEndMs = 0;
    /// <ID> from XmlExporter::buildExportedInstances. Negative when this clip has none.
    int xmlInstanceId = -1;
    QVector<ClipSidecarLabel> labels;
};

namespace CompilationSidecar {

/// Safety margin for the final-file check. One frame is the spec; two frames
/// absorbs container rounding on the concatenated mp4.
inline constexpr int kDurationToleranceFrames = 2;
inline constexpr int kClipsSchemaVersion = 1;

struct OffsetAccumulation {
    QVector<qint64> offsetMilliseconds;
    qint64 summedEndMilliseconds = 0;
};

struct SidecarClip {
    int ordinal = 0;
    qint64 offsetMilliseconds = 0;
    qint64 durationMilliseconds = 0;
    qint64 sourceStartMilliseconds = 0;
    qint64 sourceEndMilliseconds = 0;
    int xmlInstanceId = -1;
    QString periodLabel;
    QVector<ClipSidecarLabel> labels;
};

struct SidecarDocument {
    QString avaVersion;
    QString videoFileName;
    QString sourceVideoFileName;
    /// Empty when this compilation did not export or link a match XML file.
    QString matchXmlFileName;
    bool framesPerSecondKnown = false;
    double framesPerSecond = 0.0;
    bool durationKnown = false;
    qint64 durationMilliseconds = 0;
    bool verified = false;
    QString team;
    QString code;
    QString overlayLabel;
    QVector<SidecarClip> clips;
};

struct SidecarBuildInput {
    QString avaVersion;
    QString videoFileName;
    QString sourceVideoFileName;
    QString matchXmlFileName;
    std::optional<double> finalDurationSeconds;
    std::optional<double> framesPerSecond;
    /// Measured duration of each re-encoded segment, in concat order.
    QVector<double> measuredDurationSeconds;
    /// Same order as measuredDurationSeconds. Not re-sorted.
    QVector<ClipCompilationRecord> clipsInConcatOrder;
};

qint64 roundSecondsToMilliseconds(double seconds);
QString formatSeconds(qint64 milliseconds);

OffsetAccumulation accumulateOffsets(const QVector<qint64>& durationMilliseconds);

bool finalDurationWithinTolerance(qint64 finalDurationMilliseconds,
                                  qint64 summedEndMilliseconds,
                                  double framesPerSecond,
                                  int toleranceFrames);

SidecarDocument buildDocument(const SidecarBuildInput& input);
QByteArray renderJson(const SidecarDocument& document);

QString chapterTitle(int ordinal, int totalCount, const QString& periodLabel);
QString renderChapterMetadata(const QVector<qint64>& offsetMilliseconds,
                              const QVector<qint64>& durationMilliseconds,
                              const QStringList& titles);

QString clipsJsonPathForVideo(const QString& videoPath);
bool writeUtf8FileAtomically(const QString& filePath,
                             const QByteArray& utf8Bytes,
                             QString* errorMessage = nullptr);

std::optional<double> probeMediaDurationSeconds(const QString& ffprobePath,
                                                const QString& mediaPath);
std::optional<double> probeVideoFramesPerSecond(const QString& ffprobePath,
                                                const QString& mediaPath);

}  // namespace CompilationSidecar
