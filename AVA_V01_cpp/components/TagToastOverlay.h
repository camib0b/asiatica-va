#pragma once

#include <QColor>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QWidget>

class QLabel;
class QTimer;

/// Confirmation toast pinned to the top edge of the video frame.
///
/// A frameless tool window so it stays above the video picture and does not take clicks.
/// It does not take focus and does not accept mouse input, so playback and the
/// controls bar keep working through it.
///
/// A main event replaces the chain. Follow-up answers append until the chain
/// closes, then the toast leaves on its own. Esc dismisses immediately.
class TagToastOverlay final : public QWidget {
  Q_OBJECT

public:
  /// Single key with no follow-ups: long enough to read, short enough to feel like a flash.
  static constexpr int kPassingDismissMilliseconds = 720;
  /// After the last follow-up, stay a little longer so the whole chain can be read.
  static constexpr int kChainCompleteDismissMilliseconds = 1050;
  static constexpr int kTopMarginPixels = 10;
  /// Keeps a pathological chain from covering the picture. Real tag paths fit inside this.
  static constexpr int kMaximumBodyHeightPixels = 96;

  explicit TagToastOverlay(QWidget* parent = nullptr);

  /// Follows \p videoFrame while it moves or resizes. Null clears the anchor.
  void trackVideoFrame(QWidget* videoFrame);

  /// Starts a new chain for a main event. \p chainOpen is false when the event has no follow-ups.
  void beginEvent(const QString& shortcutKey, const QString& eventName, const QColor& teamColor,
                  bool chainOpen);

  /// Appends one follow-up answer. Ignored until \c beginEvent has started a chain.
  void appendFollowUp(const QString& answer, bool chainOpen);

  /// Hides immediately and forgets the chain. Used for Esc.
  void dismissImmediately();

  /// Hides without forgetting an open chain (tagging panel left the screen).
  void suspend();

  /// Shows the toast again when an open chain was only suspended.
  void resumeOpenChain();

  QString chainText() const;
  QString shortcutKey() const;
  /// Key and chain the way a reader would say it: "D  Shot › On target › Saved".
  QString displayLine() const;
  QColor teamColor() const { return teamColor_; }
  bool isChainOpen() const { return chainOpen_; }
  bool isPresented() const { return presented_; }

  static QString chainSeparator();

protected:
  void paintEvent(QPaintEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  void present();
  void reposition();
  void refreshText();
  void clearSequence();
  void startDismiss(bool immediate);
  void scheduleDismiss();
  void showWithoutTakingFocus();

  QPointer<QWidget> videoFrame_;
  QPointer<QWidget> anchorWindow_;
  QPointer<QLabel> chainLabel_;
  QWidget* keyChip_ = nullptr;
  QTimer* dismissTimer_ = nullptr;
  QStringList steps_;
  QString shortcutKey_;
  QColor teamColor_;
  bool chainOpen_ = false;
  bool presented_ = false;
  bool repositioning_ = false;
  int presentationGeneration_ = 0;
};
