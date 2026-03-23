#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

class QTemporaryDir;

struct ClipSegment {
    qint64 startMs;
    qint64 durationMs;
    QString overlayText;
    QString secondaryOverlayText;
};

class ClipExporter final : public QObject {
    Q_OBJECT

public:
    explicit ClipExporter(QObject* parent = nullptr);
    ~ClipExporter() override;

    void setSourceVideo(const QString& path);
    void setOutputPath(const QString& path);
    void setClips(const QVector<ClipSegment>& clips);

    void startExport();
    void cancelExport();

    bool isRunning() const;
    static QString findFfmpeg();

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
                                        const QString& outputPath);
    static QString generateBrandingImage(const QString& outputPath);

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
};
