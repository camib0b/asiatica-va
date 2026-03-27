#pragma once

#include <QElapsedTimer>
#include <QWidget>
#include <QtGlobal>

class QLabel;
class QSlider;
class QLineEdit;
class QEvent;

class TimelineBar final : public QWidget {
  Q_OBJECT
public:
  explicit TimelineBar(QWidget* parent = nullptr);

  void reset();                       // 00:00 / 00:00, disabled slider
  void setEnabledForMedia(bool on);   // main enable toggle
  void setDurationMs(qint64 durMs);
  void setPositionMs(qint64 posMs);

signals:
  void scrubStarted();                // user grabbed the handle
  void scrubSeekTo(qint64 posMs);     // optional throttled live seeking
  void scrubFinished(qint64 posMs);   // definitive seek on release/click
  void timeEntryStarted();            // user clicked timestamp entry and requested pause

private:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void buildUi();
  void wireSignals();
  void beginTimeEntry();
  void commitTimeEntry();
  void cancelTimeEntry();
  void updateLabel(qint64 posMs, qint64 durMs);
  static bool parseTimeEntryMs(const QString& text, qint64* outMs);
  static QString formatMs(qint64 ms);

  QSlider* slider_ = nullptr;
  QLabel* label_ = nullptr;
  QLineEdit* timeEntry_ = nullptr;

  qint64 durationMs_ = 0;
  qint64 lastKnownPositionMs_ = 0;
  bool isScrubbing_ = false;
  bool isEditingTimeEntry_ = false;
  bool enableLiveScrubSeek_ = true;   // you can tweak this later
  qint64 pendingSeekMs_ = -1;
  bool waitingForSeekCommit_ = false;
  QElapsedTimer scrubThrottleTimer_;
};
