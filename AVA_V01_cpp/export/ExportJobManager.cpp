#include "ExportJobManager.h"

#include "AppLocale.h"
#include "ClipExporter.h"
#include "ExportClipBuilder.h"
#include "TagSession.h"
#include "XmlExporter.h"
#include "YouTubeAuthManager.h"
#include "YouTubeUploader.h"

#include <QDir>
#include <QFileInfo>

ExportJobManager::ExportJobManager(YouTubeAuthManager* youtubeAuth, QObject* parent)
    : QObject(parent)
    , youtubeAuth_(youtubeAuth) {
}

ExportJobManager::~ExportJobManager() {
    for (Job* job : jobs_) {
        if (job->exporter) job->exporter->cancelExport();
        if (job->uploader) job->uploader->cancelUpload();
        delete job;
    }
    jobs_.clear();
}

QString ExportJobManager::canonicalPath(const QString& path) {
    return QFileInfo(path).absoluteFilePath();
}

bool ExportJobManager::pathIsOccupied(const QString& path) const {
    if (path.trimmed().isEmpty()) return false;
    const QString candidate = canonicalPath(path);
    for (const Job* job : jobs_) {
        if (!job) continue;
        const bool running = job->state == JobState::Exporting
            || job->state == JobState::ResolvingPlaylist
            || job->state == JobState::Uploading;
        if (!running) continue;
        if (!job->outputPath.isEmpty() && canonicalPath(job->outputPath) == candidate) return true;
        if (!job->xmlPath.isEmpty() && canonicalPath(job->xmlPath) == candidate) return true;
    }
    return false;
}

QStringList ExportJobManager::activeOutputPaths() const {
    QStringList paths;
    for (const Job* job : jobs_) {
        if (!job) continue;
        const bool running = job->state == JobState::Exporting
            || job->state == JobState::ResolvingPlaylist
            || job->state == JobState::Uploading;
        if (!running) continue;
        if (!job->outputPath.isEmpty()) paths.append(canonicalPath(job->outputPath));
        if (!job->xmlPath.isEmpty()) paths.append(canonicalPath(job->xmlPath));
    }
    return paths;
}

ExportJobManager::Job* ExportJobManager::jobById(int jobId) {
    for (Job* job : jobs_) {
        if (job && job->id == jobId) return job;
    }
    return nullptr;
}

const ExportJobManager::Job* ExportJobManager::jobById(int jobId) const {
    for (const Job* job : jobs_) {
        if (job && job->id == jobId) return job;
    }
    return nullptr;
}

bool ExportJobManager::hasJobs() const {
    return !jobs_.isEmpty();
}

ExportJobSnapshot ExportJobManager::snapshotFor(const Job& job) const {
    ExportJobSnapshot snapshot;
    snapshot.id = job.id;
    snapshot.displayName = job.displayName;
    snapshot.statusText = job.statusText;
    snapshot.youtubeUrl = job.youtubeUrl;
    snapshot.failed = job.state == JobState::Failed || job.state == JobState::Cancelled;
    snapshot.running = job.state == JobState::Exporting
        || job.state == JobState::ResolvingPlaylist
        || job.state == JobState::Uploading;
    snapshot.canCancel = snapshot.running;
    snapshot.canDismiss = !snapshot.running;

    if (job.state == JobState::Uploading) {
        snapshot.progressPercent = job.uploadPercent;
    } else if (job.totalClips > 0 && job.state == JobState::Exporting) {
        snapshot.progressPercent = qMin(100, (job.currentClip * 100) / job.totalClips);
    } else if (job.state == JobState::Succeeded) {
        snapshot.progressPercent = 100;
    } else {
        snapshot.progressPercent = 0;
    }
    return snapshot;
}

QVector<ExportJobSnapshot> ExportJobManager::snapshots() const {
    QVector<ExportJobSnapshot> result;
    result.reserve(jobs_.size());
    for (const Job* job : jobs_) {
        if (job) result.append(snapshotFor(*job));
    }
    return result;
}

void ExportJobManager::updateExportingStatus(Job& job) {
    job.statusText = QStringLiteral("%1 %2 / %3")
        .arg(AppLocale::trUi("export.progress_prefix"))
        .arg(job.currentClip)
        .arg(job.totalClips);
}

void ExportJobManager::finishJob(Job& job, JobState state, const QString& message) {
    job.state = state;
    job.statusText = message;
    if (job.exporter) {
        job.exporter->deleteLater();
        job.exporter = nullptr;
    }
    if (job.uploader) {
        job.uploader->deleteLater();
        job.uploader = nullptr;
    }
    emit jobsChanged();
}

void ExportJobManager::startYouTubeUploadIfReady(Job& job) {
    if (!job.uploadToYouTube || !job.uploader) {
        finishJob(job, JobState::Succeeded, AppLocale::trUi("export.done"));
        return;
    }
    if (!job.playlistResolved) {
        job.state = JobState::ResolvingPlaylist;
        job.statusText = AppLocale::trUi("export.youtube_resolving_playlist");
        emit jobsChanged();
        return;
    }
    if (job.playlistId.isEmpty()) {
        const QString error = job.playlistError.isEmpty()
            ? AppLocale::trUi("export.youtube_upload_failed")
            : job.playlistError;
        finishJob(job, JobState::Failed, error);
        return;
    }

    job.state = JobState::Uploading;
    job.uploadPercent = 0;
    job.statusText = AppLocale::trUi("export.youtube_uploading");
    emit jobsChanged();
    job.uploader->uploadVideo(job.outputPath, job.youtubeMetadata, job.playlistId);
}

bool ExportJobManager::startJob(const ExportJobRequest& request, QString* errorMessage) {
    auto setError = [errorMessage](const QString& text) {
        if (errorMessage) *errorMessage = text;
    };

    const QString chosenPath = request.outputPath.trimmed();
    if (chosenPath.isEmpty()) {
        setError(AppLocale::trUi("export.no_output_path"));
        return false;
    }

    QString xmlPath;
    QString mp4Path;
    if (request.format == ExportOutputFormat::Xml) {
        QFileInfo info(chosenPath);
        xmlPath = (info.suffix().toLower() == QLatin1String("xml"))
            ? chosenPath
            : QDir(info.absolutePath()).filePath(info.completeBaseName() + QStringLiteral(".xml"));
    } else {
        mp4Path = chosenPath;
        if (request.format == ExportOutputFormat::Both) {
            QFileInfo info(chosenPath);
            xmlPath = QDir(info.absolutePath())
                .filePath(ExportClipBuilder::xmlReportBaseName(request.tagSession)
                          + QStringLiteral(".xml"));
        }
    }

    if (pathIsOccupied(mp4Path) || pathIsOccupied(xmlPath)) {
        setError(AppLocale::trUi("export.job_path_in_use"));
        return false;
    }

    if (request.format != ExportOutputFormat::Xml) {
        if (request.clips.isEmpty()) {
            setError(AppLocale::trUi("export.no_clips_selected"));
            return false;
        }
        if (ClipExporter::findFfmpeg().isEmpty()) {
            setError(AppLocale::trUi("export.ffmpeg_not_found"));
            return false;
        }
    }

    if ((request.format == ExportOutputFormat::Xml || request.format == ExportOutputFormat::Both)
        && !xmlPath.isEmpty()) {
        QString xmlError;
        const bool xmlOk = XmlExporter::writeAllInstances(request.tagSession, xmlPath, &xmlError);
        if (!xmlOk) {
            setError(xmlError.isEmpty() ? AppLocale::trUi("export.xml_failed") : xmlError);
            return false;
        }
    }

    auto* job = new Job();
    job->id = nextJobId_++;
    job->format = request.format;
    job->outputPath = mp4Path.isEmpty() ? xmlPath : mp4Path;
    job->xmlPath = xmlPath;
    job->displayName = QFileInfo(job->outputPath).fileName();
    job->uploadToYouTube = request.uploadToYouTube && request.format != ExportOutputFormat::Xml;
    job->youtubeMetadata = request.youtubeMetadata;
    job->totalClips = request.clips.size();
    jobs_.append(job);

    if (request.format == ExportOutputFormat::Xml) {
        job->state = JobState::Succeeded;
        job->statusText = AppLocale::trUi("export.xml_success");
        emit jobsChanged();
        return true;
    }

    job->state = JobState::Exporting;
    job->currentClip = 0;
    updateExportingStatus(*job);

    auto* exporter = new ClipExporter(this);
    job->exporter = exporter;
    exporter->setSourceVideo(request.sourceVideoPath);
    exporter->setOutputPath(mp4Path);
    exporter->setClips(request.clips);
    exporter->setIncludeAudioTrack(request.includeAudioTrack);
    exporter->setIncludeBrandingOverlay(request.includeAvaOverlay);

    connect(exporter, &ClipExporter::progressChanged, this,
            [this, jobId = job->id](int currentClip, int totalClips) {
        Job* currentJob = jobById(jobId);
        if (!currentJob) return;
        currentJob->currentClip = currentClip;
        currentJob->totalClips = totalClips;
        updateExportingStatus(*currentJob);
        emit jobsChanged();
    });
    connect(exporter, &ClipExporter::exportFinished, this,
            [this, jobId = job->id](bool success, const QString& message) {
        Job* currentJob = jobById(jobId);
        if (!currentJob) return;
        if (currentJob->exporter) {
            currentJob->exporter->deleteLater();
            currentJob->exporter = nullptr;
        }
        if (!success) {
            const bool cancelled = message.contains(QStringLiteral("cancelled"), Qt::CaseInsensitive);
            finishJob(*currentJob,
                      cancelled ? JobState::Cancelled : JobState::Failed,
                      cancelled ? AppLocale::trUi("export.job_cancelled") : message);
            return;
        }
        startYouTubeUploadIfReady(*currentJob);
    });

    if (job->uploadToYouTube && youtubeAuth_) {
        auto* uploader = new YouTubeUploader(youtubeAuth_, this);
        job->uploader = uploader;
        connect(uploader, &YouTubeUploader::progressChanged, this,
                [this, jobId = job->id](int percent) {
            Job* currentJob = jobById(jobId);
            if (!currentJob) return;
            currentJob->uploadPercent = percent;
            currentJob->statusText = AppLocale::trUi("export.youtube_uploading");
            emit jobsChanged();
        });
        connect(uploader, &YouTubeUploader::uploadFinished, this,
                [this, jobId = job->id](bool success, const QString& message, const QString& videoUrl) {
            Job* currentJob = jobById(jobId);
            if (!currentJob) return;
            currentJob->youtubeUrl = videoUrl;
            if (success) {
                finishJob(*currentJob, JobState::Succeeded,
                          AppLocale::trUi("export.youtube_upload_done"));
            } else {
                finishJob(*currentJob, JobState::Failed,
                          message.isEmpty() ? AppLocale::trUi("export.youtube_upload_failed") : message);
            }
        });

        uploader->resolvePlaylistForMatch(request.tagSession,
            [this, jobId = job->id](const QString& playlistId, const QString& error) {
                Job* currentJob = jobById(jobId);
                if (!currentJob) return;
                currentJob->playlistResolved = true;
                currentJob->playlistId = playlistId;
                currentJob->playlistError = error;
                if (currentJob->state == JobState::ResolvingPlaylist) {
                    startYouTubeUploadIfReady(*currentJob);
                }
            });
    }

    emit jobsChanged();
    exporter->startExport();
    return true;
}

void ExportJobManager::cancelJob(int jobId) {
    Job* job = jobById(jobId);
    if (!job) return;
    const bool running = job->state == JobState::Exporting
        || job->state == JobState::ResolvingPlaylist
        || job->state == JobState::Uploading;
    if (!running) return;

    if (job->exporter) job->exporter->cancelExport();
    if (job->uploader) job->uploader->cancelUpload();
    finishJob(*job, JobState::Cancelled, AppLocale::trUi("export.job_cancelled"));
}

void ExportJobManager::dismissJob(int jobId) {
    for (int index = 0; index < jobs_.size(); ++index) {
        Job* job = jobs_.at(index);
        if (!job || job->id != jobId) continue;
        const bool running = job->state == JobState::Exporting
            || job->state == JobState::ResolvingPlaylist
            || job->state == JobState::Uploading;
        if (running) return;
        jobs_.removeAt(index);
        delete job;
        emit jobsChanged();
        return;
    }
}
