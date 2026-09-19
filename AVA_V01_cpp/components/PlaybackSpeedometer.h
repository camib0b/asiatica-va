#pragma once

#include <QWidget>

class PlaybackSpeedometer final : public QWidget {
  Q_OBJECT

public:
  explicit PlaybackSpeedometer(QWidget* parent = nullptr);

  void setRate(double rate);
  double rate() const { return rate_; }

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
  QRect trackRect() const;
  int indexFromX(int x) const;
  int xForIndex(int index) const;
  void applyIndexFromX(int x);
  void setFlashing(bool flashing);

  double rate_ = 1.0;
  bool dragging_ = false;
  bool flashing_ = false;
};
