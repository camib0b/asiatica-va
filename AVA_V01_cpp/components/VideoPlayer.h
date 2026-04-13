#pragma once

#include <QMediaPlayer>
#include <QWidget>

class QVideoWidget;
class QAudioOutput;
class QMediaDevices;
class QAction;
class QTimer;
class VideoControlsBar;
class TimelineBar;

class VideoPlayer final : public QWidget {
  Q_OBJECT

public:
  explicit VideoPlayer(QWidget* parent = nullptr);
  ~VideoPlayer() override;

  void loadVideoFromFile(const QString& filePath);
  QVideoWidget* videoWidget() const { return videoWidget_; }
  VideoControlsBar* controlsBar() const { return videoControlsBar_; }
  TimelineBar* timelineBar() const { return videoTimelineBar_; }

  qint64 currentPositionMs() const;
  qint64 durationMs() const { return durationMs_; }
  void seekToMs(qint64 posMs);

  void setControlsVisible(bool visible);
  void setControlsEnabled(bool enabled);
  void setPlaybackKeyboardShortcutsEnabled(bool enabled);

  /// True when keyboard shortcuts should drive playback (media loaded and not temporarily disabled).
  bool isMediaKeyboardControlActive() const {
    return mediaControlsEnabled_ && playbackKeyboardShortcutsEnabled_;
  }

  /// Play/pause with the same visual feedback as the controls bar (used when shortcuts are handled outside this widget).
  void togglePlayPauseWithControlFlash();
  void playbackSlowerWithControlFlash();
  void playbackFasterWithControlFlash();
  void playbackResetSpeedWithControlFlash();

signals:
  void videoClosed();
  void positionChangedMs(qint64 positionMs);

private slots:
  void onPlayClicked();
  void onPauseClicked();
  void onFasterClicked();
  void onSlowerClicked();
  void onResetSpeedClicked();
  void onMuteToggled(bool muted);
  void onTogglePlayPause();
  void onSeekSmallBackward();
  void onSeekSmallForward();
  void onSeekBigBackward();
  void onSeekBigForward();
  void onAudioOutputsChanged();

protected:
  bool eventFilter(QObject* obj, QEvent* event) override;

private:
  void buildUi();
  void wireSignals();
  void buildKeyboardShortcuts();
  void updatePlaybackShortcutActionStates();

  void seekByMs(qint64 deltaMs);
  void setPlaybackRateAndPlay(double rate);
  void setupPlaybackReliabilityHooks();
  void updateStallMonitorForPlaybackState(QMediaPlayer::PlaybackState state);
  void nudgePlaybackAfterBackendStall();
  void reloadCurrentMediaFromDisk();

  QMediaPlayer* player_ = nullptr;
  QAudioOutput* audioOutput_ = nullptr;
  QMediaDevices* mediaDevices_ = nullptr;

  VideoControlsBar* videoControlsBar_ = nullptr;
  TimelineBar* videoTimelineBar_ = nullptr;
  QVideoWidget* videoWidget_ = nullptr;

  // keyboard shortcuts (seek arrows only; play/speed keys are handled in WorkWindow — see buildKeyboardShortcuts):
  QAction* seekSmallBackAction_ = nullptr;
  QAction* seekSmallForwardAction_ = nullptr;
  QAction* seekBigBackAction_ = nullptr;
  QAction* seekBigForwardAction_ = nullptr;

  bool mediaControlsEnabled_ = false;
  bool playbackKeyboardShortcutsEnabled_ = true;

  // Settings or constants:
  double playbackRate_ = 1.0;
  bool wasPlayingBeforeScrub_ = false;
  qint64 durationMs_ = 0;

  QTimer* playbackStallTimer_ = nullptr;
  QString loadedSourcePath_;
  qint64 lastStallCheckPositionMs_ = -1;
  int consecutivePlaybackStallTicks_ = 0;
  bool userRequestedPlaying_ = false;
};
