#pragma once

#include <QWidget>

class QVideoWidget;
class QMediaPlayer;
class QAudioOutput;
class QMediaDevices;
class QAction;
class VideoControlsBar;
class TimelineBar;

class VideoPlayer final : public QWidget {
  Q_OBJECT

public:
  explicit VideoPlayer(QWidget* parent = nullptr);
  ~VideoPlayer() override = default;

  void loadVideoFromFile(const QString& filePath);
  QVideoWidget* videoWidget() const { return videoWidget_; }
  VideoControlsBar* controlsBar() const { return videoControlsBar_; }
  TimelineBar* timelineBar() const { return videoTimelineBar_; }

  qint64 currentPositionMs() const;
  qint64 durationMs() const { return durationMs_; }
  void seekToMs(qint64 posMs);

  void setControlsVisible(bool visible);
  void setControlsEnabled(bool enabled);

signals:
  void videoClosed();

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

private:
  void buildUi();
  void wireSignals();
  void buildKeyboardShortcuts();
  
  void seekByMs(qint64 deltaMs);
  void setPlaybackRateAndPlay(double rate);

  QMediaPlayer* player_ = nullptr;
  QAudioOutput* audioOutput_ = nullptr;
  QMediaDevices* mediaDevices_ = nullptr;

  VideoControlsBar* videoControlsBar_ = nullptr;
  TimelineBar* videoTimelineBar_ = nullptr;
  QVideoWidget* videoWidget_ = nullptr;

  // keyboard shortcuts:
  QAction* slowerAction_ = nullptr;
  QAction* fasterAction_ = nullptr;
  QAction* resetSpeedAction_ = nullptr;
  QAction* togglePlayPauseAction_ = nullptr;
  QAction* seekSmallBackAction_ = nullptr;
  QAction* seekSmallForwardAction_ = nullptr;
  QAction* seekBigBackAction_ = nullptr;
  QAction* seekBigForwardAction_ = nullptr;

  // Settings or constants:
  double playbackRate_ = 1.0;
  bool wasPlayingBeforeScrub_ = false;
  qint64 durationMs_ = 0;
};
