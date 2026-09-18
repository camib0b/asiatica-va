#pragma once

#include <QWidget>
#include <QtGlobal>

class ClipTrimBar final : public QWidget {
    Q_OBJECT

public:
    explicit ClipTrimBar(QWidget* parent = nullptr);

    void configure(qint64 markMs, qint64 startMs, qint64 endMs,
                   qint64 windowStartMs, qint64 windowEndMs);
    void setPlayheadMs(qint64 playheadMs);

    qint64 clipStartMs() const { return clipStartMs_; }
    qint64 clipEndMs() const { return clipEndMs_; }

signals:
    void clipStartChanged(qint64 startMs);
    void clipEndChanged(qint64 endMs);
    void seekRequested(qint64 positionMs);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    QSize sizeHint() const override;

private:
    enum class DragTarget { None, ClipStart, ClipEnd };

    int msToX(qint64 ms) const;
    qint64 xToMs(int x) const;
    QRect startHandleRect() const;
    QRect endHandleRect() const;
    qint64 effectiveMinClipDurationMs() const;
    qint64 clampedClipStartMs(qint64 proposedStartMs) const;
    qint64 clampedClipEndMs(qint64 proposedEndMs) const;
    static QString formatMs(qint64 ms);

    qint64 windowStartMs_ = 0;
    qint64 windowEndMs_ = 0;
    qint64 markMs_ = 0;
    qint64 clipStartMs_ = 0;
    qint64 clipEndMs_ = 0;
    qint64 playheadMs_ = 0;

    DragTarget dragTarget_ = DragTarget::None;

    static constexpr int kHandleWidth = 12;
    static constexpr int kTrackHeight = 32;
    static constexpr int kMargin = 20;
    static constexpr int kLabelRowHeight = 18;
    static constexpr int kLabelGap = 4;
    static constexpr qint64 kMinClipDurationMs = 500;
};
