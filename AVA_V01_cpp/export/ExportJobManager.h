#pragma once

#include "ClipExporter.h"
#include "YouTubeUploader.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

class ClipExporter;
class TagSession;
class YouTubeAuthManager;
class YouTubeUploader;

enum class ExportOutputFormat {
    Mp4 = 0,
    Xml = 1,
    Both = 2,
};

struct ExportJobRequest {
    ExportOutputFormat format = ExportOutputFormat::Mp4;
    QString sourceVideoPath;
    QString outputPath;
    QVector<ClipSegment> clips;
    bool includeAudioTrack = true;
    bool includeAvaOverlay = true;
    bool uploadToYouTube = false;
    YouTubeUploadMetadata youtubeMetadata;
    TagSession* tagSession = nullptr;
};

struct ExportJobSnapshot {
    int id = 0;
    QString displayName;
    QString statusText;
    int progressPercent = 0;
    bool running = false;
    bool canCancel = false;
    bool canDismiss = false;
    bool failed = false;
    QString youtubeUrl;
};

class ExportJobManager final : public QObject {
    Q_OBJECT

public:
    explicit ExportJobManager(YouTubeAuthManager* youtubeAuth, QObject* parent = nullptr);
    ~ExportJobManager() override;

    /// Starts a job. Returns false and fills \p errorMessage when the request cannot be accepted
    /// (empty path, colliding output, missing ffmpeg, empty MP4 clip list, XML write failure).
    bool startJob(const ExportJobRequest& request, QString* errorMessage = nullptr);

    void cancelJob(int jobId);
    void dismissJob(int jobId);

    QVector<ExportJobSnapshot> snapshots() const;
    bool hasJobs() const;
    /// Absolute output paths (and companion XML paths) currently being written by a running job.
    QStringList activeOutputPaths() const;

signals:
    void jobsChanged();

private:
    enum class JobState {
        Exporting,
        ResolvingPlaylist,
        Uploading,
        Succeeded,
        Failed,
        Cancelled,
    };

    struct Job {
        int id = 0;
        JobState state = JobState::Exporting;
        ExportOutputFormat format = ExportOutputFormat::Mp4;
        QString outputPath;
        QString xmlPath;
        QString displayName;
        QString statusText;
        QString errorMessage;
        QString youtubeUrl;
        int currentClip = 0;
        int totalClips = 0;
        int uploadPercent = 0;
        bool uploadToYouTube = false;
        YouTubeUploadMetadata youtubeMetadata;
        QString playlistId;
        QString playlistError;
        bool playlistResolved = false;
        ClipExporter* exporter = nullptr;
        YouTubeUploader* uploader = nullptr;
    };

    Job* jobById(int jobId);
    const Job* jobById(int jobId) const;
    ExportJobSnapshot snapshotFor(const Job& job) const;
    void updateExportingStatus(Job& job);
    void finishJob(Job& job, JobState state, const QString& message);
    void startYouTubeUploadIfReady(Job& job);
    bool pathIsOccupied(const QString& path) const;
    static QString canonicalPath(const QString& path);

    YouTubeAuthManager* youtubeAuth_ = nullptr;
    QVector<Job*> jobs_;
    int nextJobId_ = 1;
};
