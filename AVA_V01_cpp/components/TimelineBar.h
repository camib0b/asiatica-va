#pragma once

#include <QWidget>
#include <QtGlobal>

class QLabel;
class QSlider;

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

private:
  void buildUi();
  void wireSignals();
  void updateLabel(qint64 posMs, qint64 durMs);
  static QString formatMs(qint64 ms);

  QSlider* slider_ = nullptr;
  QLabel* label_ = nullptr;

  qint64 durationMs_ = 0;
  bool isScrubbing_ = false;
  bool enableLiveScrubSeek_ = true;   // you can tweak this later
};
