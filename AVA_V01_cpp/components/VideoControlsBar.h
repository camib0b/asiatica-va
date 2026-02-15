#pragma once

#include <QWidget>
#include <QHash>

class QPushButton;
class QLabel;
class QTimer;

class VideoControlsBar final : public QWidget {
  Q_OBJECT

public:
  explicit VideoControlsBar(QWidget* parent = nullptr);

  void setEnabledForMedia(bool enabled);
  void setPlaying(bool playing);
  void setPlaybackRate(double rate);
  void setMuted(bool muted);

  void flashPlayButton();
  void flashPauseButton();
  void flashSeekBackButton();
  void flashSeekForwardButton();
  void flashSlowerButton();
  void flashFasterButton();
  void flashResetSpeedButton();
  void flashMuteButton();

signals:
  void playRequested();
  void pauseRequested();
  void seekRequestedMs(qint64 deltaMs);
  void slowerRequested();
  void fasterRequested();
  void resetSpeedRequested();
  void muteToggled(bool muted);

private:
  void buildUi();
  void wireSignals();
  void updateSpeedLabel();
  void flashButtonBorder(QPushButton* button);

  QHash<QPushButton*, QString>    originalButtonStyles_;
  QHash<QPushButton*, QTimer*>    flashTimers_;
  QPushButton* playButton_        = nullptr;
  QPushButton* pauseButton_       = nullptr;
  QPushButton* backButton_        = nullptr;
  QPushButton* forwardButton_     = nullptr;
  QPushButton* slowerButton_      = nullptr;
  QPushButton* fasterButton_      = nullptr;
  QPushButton* resetSpeedButton_  = nullptr;
  QPushButton* muteButton_        = nullptr;
  QLabel* speedLabel_             = nullptr;

  double playbackRate_ = 1.0;
};
