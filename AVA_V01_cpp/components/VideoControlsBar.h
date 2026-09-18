#pragma once

#include <QWidget>
#include <QHash>

class QPushButton;
class QLabel;
class QAction;

class VideoControlsBar final : public QWidget {
  Q_OBJECT

public:
  explicit VideoControlsBar(QWidget* parent = nullptr);

  /// Media-loaded gate for transport controls. Play/pause exclusivity comes from
  /// setPlaying(); either setter may be called in any order.
  void setEnabledForMedia(bool enabled);

  /// Media loaded and not temporarily disabled (e.g. export dialog); combined with focus gate for shortcuts.
  void setPlaybackShortcutMediaGate(bool enabled);

  /// WorkWindow focus / mode policy (text fields, other windows, setup screen).
  void setPlaybackShortcutFocusGate(bool allowed);

  /// Play vs pause exclusivity. Buttons stay disabled while media is not enabled.
  void setPlaying(bool playing);
  void setPlaybackRate(double rate);
  void setMuted(bool muted);

  void applyUiStrings();

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

  void togglePlayPauseFromKeyboardShortcut();

private:
  void buildUi();
  void wireSignals();
  void buildKeyboardShortcuts();
  void updatePlaybackShortcutEnablement();
  void updateEnabledState() const;
  void updateMuteButton() const;
  void updateSpeedLabel();
  void flashButtonBorder(QPushButton* button);

  QHash<QPushButton*, QString>    originalButtonStyles_;
  QPushButton* playButton_        = nullptr;
  QPushButton* pauseButton_       = nullptr;
  QPushButton* backButton_        = nullptr;
  QPushButton* forwardButton_     = nullptr;
  QPushButton* slowerButton_      = nullptr;
  QPushButton* fasterButton_      = nullptr;
  QPushButton* resetSpeedButton_  = nullptr;
  QPushButton* muteButton_        = nullptr;
  QLabel* speedLabel_             = nullptr;

  QAction* togglePlayPauseAction_ = nullptr;
  QAction* slowerPlaybackAction_ = nullptr;
  QAction* fasterPlaybackAction_ = nullptr;
  QAction* resetSpeedAction_ = nullptr;
  QAction* muteToggleAction_ = nullptr;

  bool playbackShortcutMediaGate_ = false;
  bool playbackShortcutFocusGate_ = false;
  bool mediaEnabled_ = false;
  bool playing_ = false;
  bool muted_ = false;

  double playbackRate_ = 1.0;
};
