#pragma once

#include "CompilationSidecar.h"

#include <QObject>
#include <QProcess>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

#include <memory>

class QTemporaryDir;

struct ScoreboardOverlay {
    QString homeName;
    QString awayName;
    int homeGoals = 0;
    int awayGoals = 0;
    QString homeColorHex;
    QString awayColorHex;
    /// Empty when period cannot be determined (no quarter square in exported frame).
    QString periodLabel;
};

struct TimedScoreboard {
    double activationOffsetSeconds;
    ScoreboardOverlay scoreboard;
};

struct ClipSegment {
    qint64 startMs;
    qint64 durationMs;
    QString overlayText;
    QString secondaryOverlayText;
    QVector<TimedScoreboard> scoreboards;
    /// Sidecar identity for this segment. Stays in the same order as the concat list.
    ClipCompilationRecord compilation;
};

class ClipExporter final : public QObject {
    Q_OBJECT

public:
    explicit ClipExporter(QObject* parent = nullptr);
    ~ClipExporter() override;

    void setSourceVideo(const QString& path);
    void setOutputPath(const QString& path);
    void setClips(const QVector<ClipSegment>& clips);
    void setIncludeAudioTrack(bool includeAudioTrack);
    void setIncludeBrandingOverlay(bool includeBrandingOverlay);
    /// File name only. Empty leaves "matchXml" null in the clips sidecar.
    void setMatchXmlFileName(const QString& fileName);

    void startExport();
    void cancelExport();

    /// Delegates to FfmpegLocator (bundled Helpers, then Homebrew/usr, then PATH).
    static QString findFfmpeg();
    static QString findFfprobe();

signals:
    void progressChanged(int currentClip, int totalClips);
    void exportFinished(bool success, const QString& message);

private slots:
    void onClipProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onConcatProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);

private:
    void processNextClip();
    void concatenateClips();
    void finalizeCompilationSidecar();
    void embedChapterMetadata(const QVector<qint64>& offsetMilliseconds,
                              const QVector<qint64>& durationMilliseconds);
    void discardPartialSidecar();
    void cleanup();
    void stopAndDiscardProcess();
    void finishExport(bool success, const QString& message);
    void startFfmpegJob(const QStringList& arguments, bool concatenating);

    QString sourceVideoPath_;
    QString outputPath_;
    QString matchXmlFileName_;
    QVector<ClipSegment> clips_;
    /// ffprobe durations of tempClipPaths_, in the same order.
    QVector<double> measuredSegmentDurationSeconds_;
    bool outputWriteStarted_ = false;

    /// Unparented QObject; this unique_ptr is the only owner. Replace via
    /// stopAndDiscardProcess() (release + deleteLater) so finished() cannot
    /// delete the process on its own stack.
    std::unique_ptr<QProcess> currentProcess_;
    std::unique_ptr<QTemporaryDir> tempDir_;
    int currentClipIndex_ = 0;
    bool cancelled_ = false;
    bool exportFinishedEmitted_ = false;
    QStringList tempClipPaths_;
    QString ffmpegPath_;
    QString ffprobePath_;
    QString brandingImagePath_;
    QSize sourceVideoSize_;
    int sourceRotationDegrees_ = 0;
    QSize outputVideoSize_;
    qreal overlayScale_ = 1.0;
    bool includeAudioTrack_ = true;
    bool includeBrandingOverlay_ = true;
};
