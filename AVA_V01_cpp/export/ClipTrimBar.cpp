#include "ClipTrimBar.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPen>

#include <algorithm>

ClipTrimBar::ClipTrimBar(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setFixedHeight(kTrackHeight + kLabelGap + kLabelRowHeight);
}

void ClipTrimBar::configure(qint64 tagMs, qint64 inMs, qint64 outMs,
                            qint64 windowStartMs, qint64 windowEndMs) {
    tagPositionMs_ = tagMs;
    inPointMs_ = inMs;
    outPointMs_ = outMs;
    windowStartMs_ = windowStartMs;
    windowEndMs_ = windowEndMs;
    playheadMs_ = inMs;
    dragTarget_ = DragTarget::None;
    update();
}

void ClipTrimBar::setPlayheadMs(qint64 posMs) {
    if (playheadMs_ == posMs) return;
    playheadMs_ = posMs;
    update();
}

QSize ClipTrimBar::sizeHint() const {
    return {400, kTrackHeight + kLabelGap + kLabelRowHeight};
}

int ClipTrimBar::msToX(qint64 ms) const {
    const int trackLeft = kMargin;
    const int trackRight = width() - kMargin;
    const int trackWidth = trackRight - trackLeft;
    if (windowEndMs_ <= windowStartMs_ || trackWidth <= 0) return trackLeft;
    const double fraction =
        static_cast<double>(ms - windowStartMs_) / (windowEndMs_ - windowStartMs_);
    return trackLeft + static_cast<int>(fraction * trackWidth);
}

qint64 ClipTrimBar::xToMs(int x) const {
    const int trackLeft = kMargin;
    const int trackRight = width() - kMargin;
    const int trackWidth = trackRight - trackLeft;
    if (trackWidth <= 0) return windowStartMs_;
    const double fraction =
        static_cast<double>(x - trackLeft) / trackWidth;
    const double clamped = std::clamp(fraction, 0.0, 1.0);
    return windowStartMs_
        + static_cast<qint64>(clamped * (windowEndMs_ - windowStartMs_));
}

QRect ClipTrimBar::inHandleRect() const {
    const int x = msToX(inPointMs_);
    return {x - kHandleWidth / 2, 0, kHandleWidth, kTrackHeight};
}

QRect ClipTrimBar::outHandleRect() const {
    const int x = msToX(outPointMs_);
    return {x - kHandleWidth / 2, 0, kHandleWidth, kTrackHeight};
}

QString ClipTrimBar::formatMs(qint64 ms) {
    if (ms < 0) ms = 0;
    const qint64 totalSeconds = ms / 1000;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds / 60) % 60;
    const qint64 seconds = totalSeconds % 60;
    const qint64 tenths = (ms / 100) % 10;

    if (hours > 0) {
        return QStringLiteral("%1:%2:%3.%4")
            .arg(hours)
            .arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0'))
            .arg(tenths);
    }
    return QStringLiteral("%1:%2.%3")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'))
        .arg(tenths);
}

void ClipTrimBar::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const int trackLeft = kMargin;
    const int trackRight = width() - kMargin;
    const int trackWidth = trackRight - trackLeft;
    if (trackWidth <= 0) return;

    // Track background
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(50, 50, 50));
    painter.drawRoundedRect(trackLeft, 0, trackWidth, kTrackHeight, 4, 4);

    const int inX = msToX(inPointMs_);
    const int outX = msToX(outPointMs_);

    // Selected range highlight
    painter.setBrush(QColor(147, 197, 253, 50));
    painter.drawRect(inX, 0, outX - inX, kTrackHeight);

    // Tag position marker (thin dashed line)
    const int tagX = msToX(tagPositionMs_);
    painter.setPen(QPen(QColor(255, 200, 50, 180), 1, Qt::DashLine));
    painter.drawLine(tagX, 2, tagX, kTrackHeight - 2);

    // Playhead
    if (playheadMs_ >= windowStartMs_ && playheadMs_ <= windowEndMs_) {
        const int phX = msToX(playheadMs_);
        painter.setPen(QPen(QColor(255, 255, 255), 2));
        painter.drawLine(phX, 0, phX, kTrackHeight);
    }

    // In handle
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(72, 199, 142));
    painter.drawRoundedRect(inHandleRect(), 3, 3);

    // Out handle
    painter.setBrush(QColor(248, 113, 113));
    painter.drawRoundedRect(outHandleRect(), 3, 3);

    // Time labels below track
    painter.setPen(QColor(160, 160, 160));
    QFont labelFont = font();
    labelFont.setPointSizeF(9.0);
    painter.setFont(labelFont);

    const int labelY = kTrackHeight + kLabelGap;
    const QFontMetrics fm(labelFont);

    const QString inText = formatMs(inPointMs_);
    painter.drawText(inX - fm.horizontalAdvance(inText) / 2, labelY,
                     fm.horizontalAdvance(inText) + 2, kLabelRowHeight,
                     Qt::AlignCenter, inText);

    const qint64 durationMs = outPointMs_ - inPointMs_;
    const double durationSec = durationMs / 1000.0;
    const QString durText = QStringLiteral("%1s").arg(durationSec, 0, 'f', 1);
    const int durX = (inX + outX) / 2;
    painter.drawText(durX - fm.horizontalAdvance(durText) / 2, labelY,
                     fm.horizontalAdvance(durText) + 2, kLabelRowHeight,
                     Qt::AlignCenter, durText);

    const QString outText = formatMs(outPointMs_);
    painter.drawText(outX - fm.horizontalAdvance(outText) / 2, labelY,
                     fm.horizontalAdvance(outText) + 2, kLabelRowHeight,
                     Qt::AlignCenter, outText);
}

void ClipTrimBar::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    const QPoint pos = event->pos();
    const int hitTolerance = 6;

    const QRect inRect = inHandleRect().adjusted(-hitTolerance, 0, hitTolerance, 0);
    const QRect outRect = outHandleRect().adjusted(-hitTolerance, 0, hitTolerance, 0);

    if (inRect.contains(pos) && outRect.contains(pos)) {
        const int distIn = std::abs(pos.x() - inHandleRect().center().x());
        const int distOut = std::abs(pos.x() - outHandleRect().center().x());
        dragTarget_ = (distIn <= distOut) ? DragTarget::InPoint : DragTarget::OutPoint;
    } else if (inRect.contains(pos)) {
        dragTarget_ = DragTarget::InPoint;
    } else if (outRect.contains(pos)) {
        dragTarget_ = DragTarget::OutPoint;
    } else {
        dragTarget_ = DragTarget::None;
        if (pos.y() <= kTrackHeight) {
            const qint64 clickMs = xToMs(pos.x());
            emit seekRequested(clickMs);
        }
    }
}

void ClipTrimBar::mouseMoveEvent(QMouseEvent* event) {
    if (dragTarget_ == DragTarget::None) {
        const QPoint pos = event->pos();
        const int hitTolerance = 6;
        const QRect inRect = inHandleRect().adjusted(-hitTolerance, 0, hitTolerance, 0);
        const QRect outRect = outHandleRect().adjusted(-hitTolerance, 0, hitTolerance, 0);
        if (inRect.contains(pos) || outRect.contains(pos)) {
            setCursor(Qt::SizeHorCursor);
        } else {
            setCursor(Qt::ArrowCursor);
        }
        return;
    }

    const qint64 rawMs = xToMs(event->pos().x());

    if (dragTarget_ == DragTarget::InPoint) {
        const qint64 maxIn = outPointMs_ - kMinClipDurationMs;
        const qint64 clamped = std::clamp(rawMs, windowStartMs_, maxIn);
        if (clamped != inPointMs_) {
            inPointMs_ = clamped;
            update();
            emit inPointChanged(inPointMs_);
            emit seekRequested(inPointMs_);
        }
    } else if (dragTarget_ == DragTarget::OutPoint) {
        const qint64 minOut = inPointMs_ + kMinClipDurationMs;
        const qint64 clamped = std::clamp(rawMs, minOut, windowEndMs_);
        if (clamped != outPointMs_) {
            outPointMs_ = clamped;
            update();
            emit outPointChanged(outPointMs_);
            emit seekRequested(outPointMs_);
        }
    }
}

void ClipTrimBar::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        dragTarget_ = DragTarget::None;
    }
    QWidget::mouseReleaseEvent(event);
}
