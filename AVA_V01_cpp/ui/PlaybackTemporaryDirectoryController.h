#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

#include <memory>
#include <vector>

class QTemporaryDir;
class VideoConcatenator;
class VideoPlayer;
class ExportJobManager;
struct ExportJobRequest;

/// Owns concat, playback-prep, and export-retained temporary directories, plus the
/// concatenation job that is still running during team setup.
///
/// A directory is removed only after the player has dropped any file inside it.
/// Directories an export still reads stay alive until that export finishes.
/// Shutdown drops the open file first and does not arm another deletion timer.
class PlaybackTemporaryDirectoryController final : public QObject {
  Q_OBJECT

public:
  explicit PlaybackTemporaryDirectoryController(QObject* parent = nullptr);
  ~PlaybackTemporaryDirectoryController() override;

  void setVideoPlayer(VideoPlayer* videoPlayer);
  void setExportJobManager(ExportJobManager* exportJobManager);

  void setConcatenatedVideoTemporaryDirectory(std::unique_ptr<QTemporaryDir> directory);
  /// Releases any previous prep directory, then takes \p directory.
  void setPlaybackPrepTemporaryDirectory(std::unique_ptr<QTemporaryDir> directory);
  void setPendingConcatenation(std::unique_ptr<VideoConcatenator> concatenator);
  VideoConcatenator* pendingConcatenator() const;

  void releasePlaybackPrepTemporaryDirectory();
  /// Drops the concat directory when neither path is inside it. A path inside
  /// it is the open playback file or the export source, so the directory stays.
  void releaseConcatenatedDirectoryIfUnneeded(const QString& sourcePath,
                                             const QString& playbackPath);
  void cleanupPendingConcatenation();
  void releaseTransientResources();
  /// Window destructor path. Disconnects the player, drops the open file, then
  /// deletes directories without scheduling a retry on this object.
  void releaseForShutdown();

  void appendRetainedPlaybackDirectories(ExportJobRequest& request) const;

private:
  void releaseConcatenatedVideoTemporaryDirectory();
  void retainOrDeleteTemporaryDirectory(std::unique_ptr<QTemporaryDir>& directory);
  /// Deletes \p directory once the player is no longer reading a file inside it.
  /// When \p releaseHoldingPlayer is true, the player drops that file first.
  void queueTemporaryDirectoryForDeletion(std::unique_ptr<QTemporaryDir> directory,
                                         bool releaseHoldingPlayer);
  void deleteTemporaryDirectoriesReleasedByPlayer();
  void scheduleTemporaryDirectoryDeletion();
  bool playerMediaSourceIsInsideDirectory(const QString& directoryPath) const;
  bool exportStillNeedsDirectory(const QString& absoluteDirectoryPath) const;
  void releaseFinishedTemporaryDirectories();

  QPointer<VideoPlayer> videoPlayer_;
  QPointer<ExportJobManager> exportJobManager_;
  std::unique_ptr<QTemporaryDir> concatenatedVideoTempDir_;
  std::unique_ptr<QTemporaryDir> playbackPrepTempDir_;
  /// Temps an in-progress export still reads after the video was closed or replaced.
  std::vector<std::unique_ptr<QTemporaryDir>> retainedTemporaryDirectories_;
  /// Temps waiting until the player has dropped the file they contain.
  std::vector<std::unique_ptr<QTemporaryDir>> temporaryDirectoriesPendingDeletion_;
  bool temporaryDirectoryDeletionScheduled_ = false;
  std::unique_ptr<VideoConcatenator> pendingConcatenator_;
};
