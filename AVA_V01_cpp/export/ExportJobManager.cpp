#include "ExportJobManager.h"

#include "AppLocale.h"
#include "ClipExporter.h"
#include "ExportClipBuilder.h"
#include "FfmpegLocator.h"
#include "TagSession.h"
#include "XmlExporter.h"

#include <QDir>
#include <QFileInfo>

namespace {

struct ResolvedOutputPaths {
    QString mp4Path;
    QString xmlPath;
};

ResolvedOutputPaths resolveOutputPaths(const ExportJobRequest& request,
                                       const TagSession* exportSession) {
    ResolvedOutputPaths paths;
    const QString chosenPath = request.outputPath.trimmed();
    if (chosenPath.isEmpty()) return paths;

    if (request.format == ExportOutputFormat::Xml) {
        const QFileInfo info(chosenPath);
        paths.xmlPath = (info.suffix().toLower() == QLatin1String("xml"))
            ? chosenPath
            : QDir(info.absolutePath()).filePath(info.completeBaseName() + QStringLiteral(".xml"));
        return paths;
    }

    paths.mp4Path = chosenPath;
    if (request.format == ExportOutputFormat::Both) {
        const QFileInfo info(chosenPath);
        paths.xmlPath = QDir(info.absolutePath())
            .filePath(ExportClipBuilder::xmlReportBaseName(exportSession)
                      + QStringLiteral(".xml"));
    }
    return paths;
}

std::unique_ptr<TagSession> materializeExportSession(const TagSessionExportData& data) {
    auto session = std::make_unique<TagSession>();
    session->setGameTeams(data.homeTeamName, data.awayTeamName, data.homeTeamColor, data.awayTeamColor);
    session->setGameMetadata(data.competitionName, data.gameDate, data.homeAbbrev, data.awayAbbrev);
    if (!data.tags.isEmpty()) {
        session->importTags(data.tags, TagSession::ImportMode::Replace, -1);
    }
    return session;
}

}  // namespace

TagSessionExportData captureTagSessionExportData(const TagSession& session) {
    TagSessionExportData data;
    data.tags = session.tags();
    data.homeTeamName = session.homeTeamName();
    data.awayTeamName = session.awayTeamName();
    data.homeTeamColor = session.homeTeamColor();
    data.awayTeamColor = session.awayTeamColor();
    data.competitionName = session.competitionName();
    data.gameDate = session.gameDate();
    data.homeAbbrev = session.homeAbbrev();
    data.awayAbbrev = session.awayAbbrev();
    return data;
}

ExportJobManager::ExportJobManager(QObject* parent)
    : QObject(parent),
      jobs_(),
      nextJobId_(1) {
}

ExportJobManager::~ExportJobManager() {
    // Drop exporter connections before member destruction. ~QObject runs after
    // jobs_ is destroyed, so leaving ClipExporter unique_ptrs connected would
    // let a destructor-time signal touch a Job already being deleted.
    for (const std::unique_ptr<Job>& job : jobs_) {
        if (!job) continue;
        discardExporter(*job);
    }
}

QString ExportJobManager::canonicalPath(const QString& path) {
    if (path.trimmed().isEmpty()) return QString();
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    if (!canonical.isEmpty()) return canonical;
    return info.absoluteFilePath();
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

bool ExportJobManager::hasExportingJob() const {
    for (const std::unique_ptr<Job>& job : jobs_) {
        if (job && job->state == JobState::Exporting) return true;
    }
    return false;
}

QStringList ExportJobManager::exportingSourcePaths() const {
    QStringList paths;
    for (const std::unique_ptr<Job>& job : jobs_) {
        if (!job || job->state != JobState::Exporting) continue;
        if (!job->sourceVideoPath.isEmpty()) paths.append(job->sourceVideoPath);
    }
    return paths;
}

QStringList ExportJobManager::exportingInputDirectories() const {
    QStringList directories;
    for (const std::unique_ptr<Job>& job : jobs_) {
        if (!job || job->state != JobState::Exporting) continue;
        directories.append(job->retainedInputDirectories);
    }
    return directories;
}

void ExportJobManager::cancelExportingJobs() {
    QVector<int> jobIds;
    for (const std::unique_ptr<Job>& job : jobs_) {
        if (job && job->state == JobState::Exporting) jobIds.append(job->id);
    }
    for (const int jobId : jobIds) {
        cancelJob(jobId);
    }
}

ExportJobSnapshot ExportJobManager::snapshotFor(const Job& job) const {
    ExportJobSnapshot snapshot;
    snapshot.id = job.id;
    snapshot.displayName = job.displayName;
    snapshot.statusText = job.statusText;
    snapshot.succeeded = job.state == JobState::Succeeded;
    snapshot.failed = job.state == JobState::Failed;
    snapshot.cancelled = job.state == JobState::Cancelled;
    snapshot.running = job.state == JobState::Exporting;
    snapshot.canCancel = snapshot.running;
    snapshot.canDismiss = !snapshot.running;
    if (!job.outputPath.isEmpty()) {
        snapshot.outputDirectory = QFileInfo(job.outputPath).absolutePath();
    }

    if (job.state == JobState::Exporting) {
        if (job.currentClip <= 0 || job.totalClips <= 0) {
            snapshot.progressIndeterminate = true;
            snapshot.progressPercent = 0;
        } else {
            snapshot.progressPercent = qMin(100, (job.currentClip * 100) / job.totalClips);
        }
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
    ClipExporter* dyingExporter = job.exporter.release();
    dyingExporter->disconnect();
    dyingExporter->cancelExport();
    dyingExporter->deleteLater();
}

void ExportJobManager::finishJob(Job& job, JobState state, const QString& message) {
    if (job.state != JobState::Exporting) return;
    job.state = state;
    job.statusText = message;
    discardExporter(job);
    emit jobsChanged();
}

bool ExportJobManager::startJob(const ExportJobRequest& request, QString* errorMessage) {
    auto setError = [errorMessage](const QString& text) {
        if (errorMessage) *errorMessage = text;
    };

    std::unique_ptr<TagSession> exportSession;
    const TagSession* exportSessionPointer = nullptr;
    if (request.format != ExportOutputFormat::Mp4) {
        exportSession = materializeExportSession(request.session);
        exportSessionPointer = exportSession.get();
    }

    const ResolvedOutputPaths paths = resolveOutputPaths(request, exportSessionPointer);
    if (paths.mp4Path.isEmpty() && paths.xmlPath.isEmpty()) {
        setError(AppLocale::trUi("export.no_output_path"));
        return false;
    }

    if (pathIsOccupied(paths.mp4Path) || pathIsOccupied(paths.xmlPath)) {
        setError(AppLocale::trUi("export.job_path_in_use"));
        return false;
    }

    if (request.format != ExportOutputFormat::Xml) {
        if (request.clips.isEmpty()) {
            setError(AppLocale::trUi("export.no_clips_selected"));
            return false;
        }
        if (FfmpegLocator::findFfmpeg().isEmpty()) {
            setError(AppLocale::trUi("export.ffmpeg_not_found"));
            return false;
        }
    }

    if (request.format == ExportOutputFormat::Xml && !paths.xmlPath.isEmpty()) {
        QString xmlError;
        const bool xmlOk =
            XmlExporter::writeAllInstances(exportSessionPointer, paths.xmlPath, &xmlError);
        if (!xmlOk) {
            setError(xmlError.isEmpty() ? AppLocale::trUi("export.xml_failed") : xmlError);
            return false;
        }
    }

    const QString& mp4Path = paths.mp4Path;
    const QString& xmlPath = paths.xmlPath;

    auto job = std::make_unique<Job>();
    job->id = nextJobId_++;
    job->format = request.format;
    job->outputPath = mp4Path.isEmpty() ? xmlPath : mp4Path;
    job->xmlPath = xmlPath;
    job->sourceVideoPath = request.sourceVideoPath;
    job->retainedInputDirectories = request.retainedInputDirectories;
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
    job->exportSession = std::move(exportSession);
    TagSession* const snapshotSession = job->exportSession.get();

    job->exporter = std::make_unique<ClipExporter>();
    ClipExporter* exporter = job->exporter.get();
    exporter->setSourceVideo(request.sourceVideoPath);
    exporter->setOutputPath(mp4Path);
    exporter->setClips(request.clips);
    exporter->setIncludeAudioTrack(request.includeAudioTrack);
    exporter->setIncludeBrandingOverlay(request.includeBrandingOverlay);
    if (request.format == ExportOutputFormat::Both && !xmlPath.isEmpty()) {
        exporter->setMatchXmlFileName(QFileInfo(xmlPath).fileName());
    }

    // Heap address is stable across unique_ptr move into jobs_. Use the
    // exporter as context so Qt drops these slots when discardExporter()
    // disconnects or deleteLater() runs. Job is not a QObject, so QPointer
    // cannot observe it; Job must outlive a connected exporter.
    Job* const jobPointer = job.get();
    connect(exporter, &ClipExporter::progressChanged, exporter,
            [this, jobPointer](int currentClip, int totalClips) {
        if (jobPointer->state != JobState::Exporting) return;
        jobPointer->currentClip = currentClip;
        jobPointer->totalClips = totalClips;
        updateExportingStatus(*jobPointer);
        emit jobsChanged();
    });
    connect(exporter, &ClipExporter::exportFinished, exporter,
            [this, jobPointer, snapshotSession](bool success, const QString& message) {
        if (jobPointer->state != JobState::Exporting) return;
        if (!success) {
            const bool cancelled = message.contains(QStringLiteral("cancelled"), Qt::CaseInsensitive);
            finishJob(*jobPointer,
                      cancelled ? JobState::Cancelled : JobState::Failed,
                      cancelled ? AppLocale::trUi("export.job_cancelled") : message);
            return;
        }
        if (jobPointer->format == ExportOutputFormat::Both && !jobPointer->xmlPath.isEmpty()) {
            QString xmlError;
            if (!XmlExporter::writeAllInstances(snapshotSession, jobPointer->xmlPath, &xmlError)) {
                finishJob(*jobPointer,
                          JobState::Failed,
                          xmlError.isEmpty() ? AppLocale::trUi("export.xml_failed") : xmlError);
                return;
            }
        }
        finishJob(*jobPointer, JobState::Succeeded, AppLocale::trUi("export.done"));
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
    finishJob(*job, JobState::Cancelled, AppLocale::trUi("export.job_cancelled"));
}

void ExportJobManager::dismissJob(int jobId) {
    for (int index = 0; index < jobs_.size(); ++index) {
        Job* job = jobs_.at(index).get();
        if (!job || job->id != jobId) continue;
        if (job->state == JobState::Exporting || job->exporter) return;
        jobs_.erase(jobs_.begin() + index);
        emit jobsChanged();
        return;
    }
}
