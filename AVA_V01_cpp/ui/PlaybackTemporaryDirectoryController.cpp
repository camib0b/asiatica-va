#include "PlaybackTemporaryDirectoryController.h"

#include "../components/VideoPlayer.h"
#include "../export/ExportJobManager.h"
#include "../export/ExportOutputPath.h"
#include "../export/VideoConcatenator.h"

#include <QDir>
#include <QTemporaryDir>
#include <QTimer>

PlaybackTemporaryDirectoryController::PlaybackTemporaryDirectoryController(QObject* parent)
    : QObject(parent),
      videoPlayer_(nullptr),
      exportJobManager_(nullptr),
      temporaryDirectoryDeletionScheduled_(false) {}

PlaybackTemporaryDirectoryController::~PlaybackTemporaryDirectoryController() = default;

void PlaybackTemporaryDirectoryController::setVideoPlayer(VideoPlayer* videoPlayer) {
  videoPlayer_ = videoPlayer;
  if (!videoPlayer_) return;
  connect(videoPlayer, &VideoPlayer::playbackMediaSettled, this,
          &PlaybackTemporaryDirectoryController::deleteTemporaryDirectoriesReleasedByPlayer);
}

void PlaybackTemporaryDirectoryController::setExportJobManager(ExportJobManager* exportJobManager) {
  exportJobManager_ = exportJobManager;
  if (!exportJobManager_) return;
  connect(exportJobManager, &ExportJobManager::jobsChanged, this,
          [this]() { releaseFinishedTemporaryDirectories(); });
}

void PlaybackTemporaryDirectoryController::setConcatenatedVideoTempDir(
    std::unique_ptr<QTemporaryDir> directory) {
  retainOrDeleteTemporaryDirectory(concatenatedVideoTempDir_);
  concatenatedVideoTempDir_ = std::move(directory);
}

void PlaybackTemporaryDirectoryController::setPlaybackPrepTempDir(
    std::unique_ptr<QTemporaryDir> directory) {
  retainOrDeleteTemporaryDirectory(playbackPrepTempDir_);
  playbackPrepTempDir_ = std::move(directory);
}

void PlaybackTemporaryDirectoryController::setPendingConcatenation(
    std::unique_ptr<VideoConcatenator> concatenator) {
  pendingConcatenator_ = std::move(concatenator);
}

VideoConcatenator* PlaybackTemporaryDirectoryController::pendingConcatenator() const {
  return pendingConcatenator_.get();
}

void PlaybackTemporaryDirectoryController::releaseTransientResources() {
  if (exportJobManager_) {
    exportJobManager_->cancelExportingJobs();
  }
  cleanupPendingConcatenation();
  cleanupConcatenatedVideo();
  cleanupPlaybackPrepVideo();
  std::vector<std::unique_ptr<QTemporaryDir>> retained = std::move(retainedTemporaryDirectories_);
  for (std::unique_ptr<QTemporaryDir>& directory : retained) {
    queueTemporaryDirectoryForDeletion(std::move(directory), true);
  }
}

void PlaybackTemporaryDirectoryController::releaseForShutdown() {
  if (videoPlayer_) {
    disconnect(videoPlayer_.data(), &VideoPlayer::playbackMediaSettled, this,
               &PlaybackTemporaryDirectoryController::deleteTemporaryDirectoriesReleasedByPlayer);
    videoPlayer_->releaseLoadedMedia();
  }
  // The window is going away, so do not arm a retry timer on this object.
  temporaryDirectoryDeletionScheduled_ = true;
  queueTemporaryDirectoryForDeletion(std::move(concatenatedVideoTempDir_), true);
  queueTemporaryDirectoryForDeletion(std::move(playbackPrepTempDir_), true);
  std::vector<std::unique_ptr<QTemporaryDir>> retained = std::move(retainedTemporaryDirectories_);
  for (std::unique_ptr<QTemporaryDir>& directory : retained) {
    queueTemporaryDirectoryForDeletion(std::move(directory), true);
  }
  deleteTemporaryDirectoriesReleasedByPlayer();
}

void PlaybackTemporaryDirectoryController::cleanupConcatenatedVideo() {
  retainOrDeleteTemporaryDirectory(concatenatedVideoTempDir_);
}

void PlaybackTemporaryDirectoryController::cleanupPlaybackPrepVideo() {
  retainOrDeleteTemporaryDirectory(playbackPrepTempDir_);
}

void PlaybackTemporaryDirectoryController::retainOrDeleteTemporaryDirectory(
    std::unique_ptr<QTemporaryDir>& directory) {
  if (!directory) return;
  const QString absolutePath = QDir(directory->path()).absolutePath();
  if (exportStillNeedsDirectory(absolutePath)) {
    retainedTemporaryDirectories_.push_back(std::move(directory));
    return;
  }
  queueTemporaryDirectoryForDeletion(std::move(directory), true);
}

void PlaybackTemporaryDirectoryController::queueTemporaryDirectoryForDeletion(
    std::unique_ptr<QTemporaryDir> directory,
    bool releaseHoldingPlayer) {
  if (!directory) return;

  const QString absolutePath = QDir(directory->path()).absolutePath();
  if (releaseHoldingPlayer && playerMediaSourceIsInsideDirectory(absolutePath) && videoPlayer_) {
    videoPlayer_->releaseLoadedMedia();
  }

  temporaryDirectoriesPendingDeletion_.push_back(std::move(directory));
  deleteTemporaryDirectoriesReleasedByPlayer();
  scheduleTemporaryDirectoryDeletion();
}

void PlaybackTemporaryDirectoryController::deleteTemporaryDirectoriesReleasedByPlayer() {
  std::vector<std::unique_ptr<QTemporaryDir>> stillPending;
  stillPending.reserve(temporaryDirectoriesPendingDeletion_.size());
  for (std::unique_ptr<QTemporaryDir>& directory : temporaryDirectoriesPendingDeletion_) {
    if (!directory) continue;
    const QString absolutePath = QDir(directory->path()).absolutePath();
    if (playerMediaSourceIsInsideDirectory(absolutePath)) {
      stillPending.push_back(std::move(directory));
      continue;
    }
    if (!directory->remove()) {
      stillPending.push_back(std::move(directory));
      continue;
    }
    directory->setAutoRemove(false);
  }
  temporaryDirectoriesPendingDeletion_ = std::move(stillPending);
}

void PlaybackTemporaryDirectoryController::scheduleTemporaryDirectoryDeletion() {
  if (temporaryDirectoryDeletionScheduled_ || temporaryDirectoriesPendingDeletion_.empty()) return;
  temporaryDirectoryDeletionScheduled_ = true;
  QTimer::singleShot(0, this, [this]() {
    temporaryDirectoryDeletionScheduled_ = false;
    deleteTemporaryDirectoriesReleasedByPlayer();
  });
}

bool PlaybackTemporaryDirectoryController::playerMediaSourceIsInsideDirectory(
    const QString& directoryPath) const {
  if (!videoPlayer_) return false;
  return ExportOutputPath::pathIsInsideDirectory(videoPlayer_->activeMediaFilePath(), directoryPath);
}

bool PlaybackTemporaryDirectoryController::exportStillNeedsDirectory(
    const QString& absoluteDirectoryPath) const {
  if (!exportJobManager_) return false;
  return ExportOutputPath::directoryIsNeededByExport(
      absoluteDirectoryPath,
      exportJobManager_->exportingInputDirectories(),
      exportJobManager_->exportingSourcePaths());
}

void PlaybackTemporaryDirectoryController::releaseFinishedTemporaryDirectories() {
  std::vector<std::unique_ptr<QTemporaryDir>> stillNeeded;
  std::vector<std::unique_ptr<QTemporaryDir>> finished;
  stillNeeded.reserve(retainedTemporaryDirectories_.size());
  for (std::unique_ptr<QTemporaryDir>& directory : retainedTemporaryDirectories_) {
    if (!directory) continue;
    const QString absolutePath = QDir(directory->path()).absolutePath();
    if (exportStillNeedsDirectory(absolutePath)) {
      stillNeeded.push_back(std::move(directory));
    } else {
      finished.push_back(std::move(directory));
    }
  }
  retainedTemporaryDirectories_ = std::move(stillNeeded);
  for (std::unique_ptr<QTemporaryDir>& directory : finished) {
    queueTemporaryDirectoryForDeletion(std::move(directory), false);
  }
}

void PlaybackTemporaryDirectoryController::cleanupPendingConcatenation() {
  pendingConcatenator_.reset();
}

void PlaybackTemporaryDirectoryController::appendRetainedPlaybackDirectories(
    ExportJobRequest& request) const {
  if (concatenatedVideoTempDir_) {
    request.retainedInputDirectories.append(
        QDir(concatenatedVideoTempDir_->path()).absolutePath());
  }
  if (playbackPrepTempDir_) {
    request.retainedInputDirectories.append(QDir(playbackPrepTempDir_->path()).absolutePath());
  }
}
