#pragma once

#include "CompilationSidecar.h"
#include "ShootoutOverlay.h"

#include <QEvent>
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
    /// Shoot-out board under the match scoreboard. Empty when this clip is before
    /// the first S.O., or when the scoreboard overlay is off.
    QVector<TimedShootoutOverlay> shootouts;
    /// Sidecar identity for this segment. Stays in the same order as the concat list.
    ClipCompilationRecord compilation;
};

class ClipExporter final : public QObject {
    Q_OBJECT

public:
    enum class Activity {
        Preparing,
        ExportingClip,
        JoiningClips,
        Finishing,
    };
    Q_ENUM(Activity)

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

    /// Null unless a test installs it. Called between steps after the joined
    /// file may already be at the output path, so the test can cancel there.
    static void setExportStepObserverForTests(void (*observer)(void* context), void* context);

    /// Delegates to FfmpegLocator (bundled Helpers, then Homebrew/usr, then PATH).
    static QString findFfmpeg();
    static QString findFfprobe();

signals:
    void progressChanged(int currentClip, int totalClips, Activity activity);
    void exportFinished(bool success, const QString& message);

private slots:
    void onClipProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onConcatProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);

private:
    void onEncoderAvailability(bool h264VideoToolboxAvailable);
    void prepareSourceAndExportClips();
    void processNextClip();
    void concatenateClips();
    /// Moves a finished temp video onto the destination. A failed replace leaves
    /// the previous file in place. Cancel after a successful replace removes the
    /// new file and puts a replaced file back.
    void commitStagedOutput(const QString& stagedPath);
    void finalizeCompilationSidecar();
    bool finishExportIfCancelled();
    bool parkExistingSidecar();
    void removeCommittedOutputAndRestorePrevious();
    void discardReplacedBackups();
    void discardPartialSidecar();
    void cleanup();
    void stopAndDiscardProcess();
    void finishExport(bool success, const QString& message);
    void startFfmpegJob(const QStringList& arguments, bool concatenating);
    bool event(QEvent* event) override;

    /// Swallows DeferredDelete while commit/sidecar steps pump the event loop,
    /// then posts it again when those steps return.
    class SuppressDeferredDelete {
    public:
        explicit SuppressDeferredDelete(ClipExporter* exporter);
        ~SuppressDeferredDelete();
        SuppressDeferredDelete(const SuppressDeferredDelete&) = delete;
        SuppressDeferredDelete& operator=(const SuppressDeferredDelete&) = delete;

    private:
        ClipExporter* exporter_;
    };

    QString sourceVideoPath_;
    QString outputPath_;
    QString matchXmlFileName_;
    QVector<ClipSegment> clips_;
    /// ffprobe durations of tempClipPaths_, in the same order.
    QVector<double> measuredSegmentDurationSeconds_;
    bool outputWriteStarted_ = false;
    /// Previous output moved aside when this export replaced it. Empty when
    /// the output path was new. Restored on cancel, deleted on success.
    QString replacedOutputBackupPath_;
    /// Previous clips sidecar moved aside for the same reason.
    QString replacedSidecarBackupPath_;
    bool sidecarWrittenByThisExport_ = false;
    bool suppressDeferredDelete_ = false;
    bool deferredDeletePending_ = false;

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
    /// Cached `ffmpeg -encoders` result for this export's ffmpeg binary.
    bool h264VideoToolboxAvailable_ = false;
    /// After VideoToolbox fails, the retry and later clips in this export use libx264.
    bool hardwareEncoderDisabledForExport_ = false;
    /// True only for the clip encode that is currently running on VideoToolbox.
    bool currentClipUsesHardwareEncoder_ = false;
};
