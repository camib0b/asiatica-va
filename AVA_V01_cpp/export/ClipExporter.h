#pragma once

#include <QObject>
#include <QProcess>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

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

    void startExport();
    void cancelExport();

    bool isRunning() const;
    static QString findFfmpeg();
    static QString findFfprobe();
    static QSize probeVideoDisplaySize(const QString& videoPath);
    static qreal computeOverlayScale(const QSize& videoSize);

signals:
    void progressChanged(int currentClip, int totalClips);
    void exportFinished(bool success, const QString& message);

private slots:
    void onClipProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onConcatProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    void processNextClip();
    void concatenateClips();
    void cleanup();
    static QString generateOverlayImage(const QString& primaryText,
                                        const QString& secondaryText,
                                        const QString& outputPath,
                                        qreal overlayScale,
                                        int maxImageWidth);
    static QString generateScoreboardImage(const ScoreboardOverlay& data,
                                           const QString& outputPath,
                                           qreal overlayScale,
                                           int maxImageWidth);
    static QString generateBrandingImage(const QString& outputPath,
                                         qreal overlayScale);

    QString sourceVideoPath_;
    QString outputPath_;
    QVector<ClipSegment> clips_;

    QProcess* currentProcess_ = nullptr;
    QTemporaryDir* tempDir_ = nullptr;
    int currentClipIndex_ = 0;
    bool cancelled_ = false;
    QStringList tempClipPaths_;
    QString ffmpegPath_;
    QString brandingImagePath_;
    QSize sourceVideoSize_;
    qreal overlayScale_ = 1.0;
    bool includeAudioTrack_ = true;
    bool includeBrandingOverlay_ = true;
};
