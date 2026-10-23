#pragma once

#include <QPoint>
#include <QRect>
#include <QString>
#include <QWidget>

class PlaybackSpeedometer final : public QWidget {
  Q_OBJECT

public:
  explicit PlaybackSpeedometer(QWidget* parent = nullptr);

  void setRate(double rate);
  double rate() const { return rate_; }

  /// Text painted to the right of the groove, for example "1.00x".
  QString displayedRateText() const;

  /// Slider groove. A click or drag here selects a playback step.
  QRect trackGrooveRect() const;
  /// Speed digits. A double-click here resets to 1.0x.
  QRect rateReadoutRect() const;

  void flash();

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

signals:
  void rateChanged(double rate);
  void interactionStarted();
  void interactionFinished();

protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
  int readoutWidthPx() const;
  int preferredHeightPx() const;
  bool isOnRateReadout(const QPoint& position) const;
  int indexFromX(int x) const;
  int xForIndex(int index) const;
  void applyIndexFromX(int x);
  void resetRate();
  void finishDrag();
  void setFlashing(bool flashing);

  double rate_ = 1.0;
  bool dragging_ = false;
  bool pressedOnReadout_ = false;
  bool flashing_ = false;
};
