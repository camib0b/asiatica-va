#pragma once

#include "ClipExporter.h"
#include "TagSession.h"

#include <QDate>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>
#include <vector>

enum class ExportOutputFormat {
    Mp4 = 0,
    Xml = 1,
    Both = 2,
};

/// Tags and game metadata copied at the moment export is requested.
/// Jobs keep this instead of a TagSession*: an MP4+XML job writes the XML
/// only after ffmpeg finishes, and the live session may already have been
/// cleared (tag ids restart at 1) or destroyed.
struct TagSessionExportData {
    QVector<TagSession::GameTag> tags;
    QString homeTeamName;
    QString awayTeamName;
    QString homeTeamColor;
    QString awayTeamColor;
    QString competitionName;
    QDate gameDate;
    QString homeAbbrev;
    QString awayAbbrev;
};

TagSessionExportData captureTagSessionExportData(const TagSession& session);

struct ExportJobRequest {
    ExportOutputFormat format = ExportOutputFormat::Mp4;
    QString sourceVideoPath;
    QString outputPath;
    QVector<ClipSegment> clips;
    bool includeAudioTrack = true;
    bool includeBrandingOverlay = true;
    TagSessionExportData session;
    /// Concat or playback-transcode directories this job reads. They must
    /// outlive the job.
    QStringList retainedInputDirectories;
};

struct ExportJobSnapshot {
    int id = 0;
    QString displayName;
    QString statusText;
    int progressPercent = 0;
    bool running = false;
    bool canCancel = false;
    bool canDismiss = false;
    bool succeeded = false;
    bool failed = false;
    bool cancelled = false;
    /// True while an MP4 job is waiting for ffmpeg to start.
    bool progressIndeterminate = false;
    QString outputDirectory;
};

class ExportJobManager final : public QObject {
    Q_OBJECT

public:
    explicit ExportJobManager(QObject* parent = nullptr);
    ~ExportJobManager() override;

    /// Starts a job. Returns false and fills \p errorMessage when the request cannot be accepted
    /// (empty path, colliding output, missing ffmpeg, empty MP4 clip list, XML write failure).
    bool startJob(const ExportJobRequest& request, QString* errorMessage = nullptr);

    void cancelJob(int jobId);
    void dismissJob(int jobId);

    QVector<ExportJobSnapshot> snapshots() const;
    bool hasJobs() const;
    bool hasExportingJob() const;
    /// Source videos still being read by a running export.
    QStringList exportingSourcePaths() const;
    /// Temp directories a running export listed as inputs.
    QStringList exportingInputDirectories() const;
    /// Cancels every running export. Partial destination files are not committed.
    void cancelExportingJobs();
    /// Absolute output paths (and companion XML paths) currently being written by a running job.
    QStringList activeOutputPaths() const;

signals:
    void jobsChanged();

private:
    enum class JobState {
        Exporting,
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
        QString sourceVideoPath;
        QStringList retainedInputDirectories;
        QString displayName;
        QString statusText;
        QString errorMessage;
        int currentClip = 0;
        int totalClips = 0;
        /// Unparented QObject; this unique_ptr is the only owner. Do not parent it
        /// to the manager. Destroy via discardExporter() (release, disconnect,
        /// cancel, deleteLater). Job must outlive a connected exporter: never
        /// erase a Job from jobs_ while exporter is non-null.
        std::unique_ptr<ClipExporter> exporter;
        /// Detached copy of the session used to write the companion XML after
        /// ffmpeg. Null for MP4-only jobs. Address is stable for the Job's life.
        std::unique_ptr<TagSession> exportSession;
    };

    Job* jobById(int jobId);
    const Job* jobById(int jobId) const;
    ExportJobSnapshot snapshotFor(const Job& job) const;
    void updateExportingStatus(Job& job);
    void finishJob(Job& job, JobState state, const QString& message);
    void discardExporter(Job& job);
    bool pathIsOccupied(const QString& path) const;
    static QString canonicalPath(const QString& path);

    std::vector<std::unique_ptr<Job>> jobs_;
    int nextJobId_ = 1;
};
