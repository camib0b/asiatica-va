#pragma once

#include <QWidget>
#include <QtGlobal>

class ClipTrimBar final : public QWidget {
    Q_OBJECT

public:
    explicit ClipTrimBar(QWidget* parent = nullptr);

    void configure(qint64 tagMs, qint64 inMs, qint64 outMs,
                   qint64 windowStartMs, qint64 windowEndMs);
    void setPlayheadMs(qint64 posMs);

    qint64 inPointMs() const { return inPointMs_; }
    qint64 outPointMs() const { return outPointMs_; }

signals:
    void inPointChanged(qint64 inMs);
    void outPointChanged(qint64 outMs);
    void seekRequested(qint64 posMs);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    QSize sizeHint() const override;

private:
    enum class DragTarget { None, InPoint, OutPoint };

    int msToX(qint64 ms) const;
    qint64 xToMs(int x) const;
    QRect inHandleRect() const;
    QRect outHandleRect() const;
    static QString formatMs(qint64 ms);

    qint64 windowStartMs_ = 0;
    qint64 windowEndMs_ = 0;
    qint64 tagPositionMs_ = 0;
    qint64 inPointMs_ = 0;
    qint64 outPointMs_ = 0;
    qint64 playheadMs_ = 0;

    DragTarget dragTarget_ = DragTarget::None;

    static constexpr int kHandleWidth = 12;
    static constexpr int kTrackHeight = 32;
    static constexpr int kMargin = 20;
    static constexpr int kLabelRowHeight = 18;
    static constexpr int kLabelGap = 4;
    static constexpr qint64 kMinClipDurationMs = 500;
};
