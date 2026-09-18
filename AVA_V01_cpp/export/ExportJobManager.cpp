#include "ExportJobManager.h"

#include "AppLocale.h"
#include "ClipExporter.h"
#include "ExportClipBuilder.h"
#include "TagSession.h"
#include "XmlExporter.h"

#include <QDir>
#include <QFileInfo>

ExportJobManager::ExportJobManager(QObject* parent)
    : QObject(parent),
      jobs_(),
      nextJobId_(1) {
}

ExportJobManager::~ExportJobManager() {
    for (const std::unique_ptr<Job>& job : jobs_) {
        if (!job || !job->exporter) continue;
        job->exporter->disconnect();
        job->exporter->cancelExport();
    }
}

QString ExportJobManager::canonicalPath(const QString& path) {
    return QFileInfo(path).absoluteFilePath();
}

bool ExportJobManager::pathIsOccupied(const QString& path) const {
    if (path.trimmed().isEmpty()) return false;
    const QString candidate = canonicalPath(path);
    for (const std::unique_ptr<Job>& job : jobs_) {
        if (!job) continue;
        if (job->state != JobState::Exporting) continue;
        if (!job->outputPath.isEmpty() && canonicalPath(job->outputPath) == candidate) return true;
        if (!job->xmlPath.isEmpty() && canonicalPath(job->xmlPath) == candidate) return true;
    }
    return false;
}

QStringList ExportJobManager::activeOutputPaths() const {
    QStringList paths;
    for (const std::unique_ptr<Job>& job : jobs_) {
        if (!job) continue;
        if (job->state != JobState::Exporting) continue;
        if (!job->outputPath.isEmpty()) paths.append(canonicalPath(job->outputPath));
        if (!job->xmlPath.isEmpty()) paths.append(canonicalPath(job->xmlPath));
    }
    return paths;
}

ExportJobManager::Job* ExportJobManager::jobById(int jobId) {
    for (const std::unique_ptr<Job>& job : jobs_) {
        if (job && job->id == jobId) return job.get();
    }
    return nullptr;
}

const ExportJobManager::Job* ExportJobManager::jobById(int jobId) const {
    for (const std::unique_ptr<Job>& job : jobs_) {
        if (job && job->id == jobId) return job.get();
    }
    return nullptr;
}

bool ExportJobManager::hasJobs() const {
    return !jobs_.empty();
}

ExportJobSnapshot ExportJobManager::snapshotFor(const Job& job) const {
    ExportJobSnapshot snapshot;
    snapshot.id = job.id;
    snapshot.displayName = job.displayName;
    snapshot.statusText = job.statusText;
    snapshot.failed = job.state == JobState::Failed || job.state == JobState::Cancelled;
    snapshot.running = job.state == JobState::Exporting;
    snapshot.canCancel = snapshot.running;
    snapshot.canDismiss = !snapshot.running;

    if (job.totalClips > 0 && job.state == JobState::Exporting) {
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
    for (const std::unique_ptr<Job>& job : jobs_) {
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

void ExportJobManager::discardExporter(Job& job) {
    if (!job.exporter) return;
    ClipExporter* dying = job.exporter.release();
    dying->disconnect();
    dying->deleteLater();
}

void ExportJobManager::finishJob(Job& job, JobState state, const QString& message) {
    job.state = state;
    job.statusText = message;
    discardExporter(job);
    emit jobsChanged();
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

    auto job = std::make_unique<Job>();
    job->id = nextJobId_++;
    job->format = request.format;
    job->outputPath = mp4Path.isEmpty() ? xmlPath : mp4Path;
    job->xmlPath = xmlPath;
    job->displayName = QFileInfo(job->outputPath).fileName();
    job->totalClips = request.clips.size();

    if (request.format == ExportOutputFormat::Xml) {
        job->state = JobState::Succeeded;
        job->statusText = AppLocale::trUi("export.xml_success");
        jobs_.push_back(std::move(job));
        emit jobsChanged();
        return true;
    }

    job->state = JobState::Exporting;
    job->currentClip = 0;
    updateExportingStatus(*job);

    job->exporter = std::make_unique<ClipExporter>();
    ClipExporter* exporter = job->exporter.get();
    exporter->setSourceVideo(request.sourceVideoPath);
    exporter->setOutputPath(mp4Path);
    exporter->setClips(request.clips);
    exporter->setIncludeAudioTrack(request.includeAudioTrack);
    exporter->setIncludeBrandingOverlay(request.includeBrandingOverlay);

    const int jobId = job->id;
    connect(exporter, &ClipExporter::progressChanged, this,
            [this, jobId](int currentClip, int totalClips) {
        Job* currentJob = jobById(jobId);
        if (!currentJob) return;
        currentJob->currentClip = currentClip;
        currentJob->totalClips = totalClips;
        updateExportingStatus(*currentJob);
        emit jobsChanged();
    });
    connect(exporter, &ClipExporter::exportFinished, this,
            [this, jobId](bool success, const QString& message) {
        Job* currentJob = jobById(jobId);
        if (!currentJob) return;
        if (!success) {
            const bool cancelled = message.contains(QStringLiteral("cancelled"), Qt::CaseInsensitive);
            finishJob(*currentJob,
                      cancelled ? JobState::Cancelled : JobState::Failed,
                      cancelled ? AppLocale::trUi("export.job_cancelled") : message);
            return;
        }
        finishJob(*currentJob, JobState::Succeeded, AppLocale::trUi("export.done"));
    });

    jobs_.push_back(std::move(job));
    emit jobsChanged();
    exporter->startExport();
    return true;
}

void ExportJobManager::cancelJob(int jobId) {
    Job* job = jobById(jobId);
    if (!job) return;
    if (job->state != JobState::Exporting) return;

    if (job->exporter) job->exporter->cancelExport();
    finishJob(*job, JobState::Cancelled, AppLocale::trUi("export.job_cancelled"));
}

void ExportJobManager::dismissJob(int jobId) {
    for (int index = 0; index < jobs_.size(); ++index) {
        Job* job = jobs_.at(index).get();
        if (!job || job->id != jobId) continue;
        if (job->state == JobState::Exporting) return;
        jobs_.erase(jobs_.begin() + index);
        emit jobsChanged();
        return;
    }
}
