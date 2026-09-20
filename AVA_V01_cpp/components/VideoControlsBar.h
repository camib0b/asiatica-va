#pragma once

#include <QWidget>

class QPushButton;
class QAction;
class PlaybackSpeedometer;

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
  bool muted() const { return muted_; }
  void toggleMute();

  void applyUiStrings();

  void flashPlayPauseButton();
  void flashSeekBackButton();
  void flashSeekForwardButton();
  void flashSpeedometer();

signals:
  void playRequested();
  void pauseRequested();
  void seekRequestedMs(qint64 deltaMs);
  void playbackRateRequested(double rate);
  void speedDragStarted();
  void speedDragFinished();
  void muteToggled(bool muted);

  void togglePlayPauseFromKeyboardShortcut();
  void toggleMuteFromKeyboardShortcut();

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  void buildUi();
  void wireSignals();
  void buildKeyboardShortcuts();
  void updatePlaybackShortcutEnablement();
  void updateEnabledState() const;
  void updatePlayPauseButton() const;
  void updateSpeedometerTooltip() const;
  void flashButtonBorder(QPushButton* button);

  QPushButton* playPauseButton_   = nullptr;
  QPushButton* backButton_        = nullptr;
  QPushButton* forwardButton_     = nullptr;
  PlaybackSpeedometer* speedometer_ = nullptr;

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
