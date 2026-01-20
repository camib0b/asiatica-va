#pragma once

#include <QWidget>

class QLabel;
class QVideoWidget;
class QMediaPlayer;
class QAudioOutput;
class QAction;
class QToolButton;
class QMenu;

class VideoControlsBar;
class TimelineBar;

class WorkWindow final : public QWidget {
  Q_OBJECT

public:
  explicit WorkWindow(QWidget* parent = nullptr);
  ~WorkWindow() override = default;

  void loadVideoFromFile(const QString& filePath);

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
  void onReplaceVideo();
  void onDiscardVideo();

private:
  void buildUi();
  void wireSignals();
  void buildKeyboardShortcuts();

  void setControlsVisible(bool visible);
  void setControlsEnabled(bool enabled);
  
  void seekByMs(qint64 deltaMs);
  void setPlaybackRateAndPlay(double rate);

  static QString formatMs(qint64 ms);
  QString promptForVideoFile();

  // discard or swap video files:
  QToolButton* videoMenuButton_ = nullptr;
  QMenu* videoMenu_ = nullptr;
  QAction* replaceVideoAction_ = nullptr;
  QAction* discardVideoAction_ = nullptr;

  // video slider:
  TimelineBar* videoTimelineBar_ = nullptr;
  bool wasPlayingBeforeScrub_ = false;
  bool isScrubbing_ = false;
  qint64 durationMs_ = 0;

  // keyboard shortcuts:
  QAction* slowerAction_ = nullptr;
  QAction* fasterAction_ = nullptr;
  QAction* resetSpeedAction_ = nullptr;
  QAction* togglePlayPauseAction_ = nullptr;
  QAction* seekSmallBackAction_ = nullptr;
  QAction* seekSmallForwardAction_ = nullptr;
  QAction* seekBigBackAction_ = nullptr;
  QAction* seekBigForwardAction_ = nullptr;

  // UI:
  QLabel* headerLabel_ = nullptr;
  VideoControlsBar* videoControlsBar_ = nullptr;
  QLabel* speedLabel_ = nullptr;
  QVideoWidget* videoWidget_ = nullptr;

  // Media:
  QMediaPlayer* player_ = nullptr;
  QAudioOutput* audioOutput_ = nullptr;

  // Settings or constants:
  double playbackRate_ = 1.0;
};
