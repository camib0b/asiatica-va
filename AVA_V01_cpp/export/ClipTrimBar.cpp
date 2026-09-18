#include "ClipTrimBar.h"

#include "../style/ThemeColors.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPen>

#include <algorithm>

ClipTrimBar::ClipTrimBar(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setFixedHeight(kTrackHeight + kLabelGap + kLabelRowHeight);
}

void ClipTrimBar::configure(qint64 markMs, qint64 startMs, qint64 endMs,
                            qint64 windowStartMs, qint64 windowEndMs) {
    markMs_ = markMs;
    windowStartMs_ = windowStartMs;
    windowEndMs_ = std::max(windowEndMs, windowStartMs);

    const qint64 minDurationMs = effectiveMinClipDurationMs();
    clipStartMs_ = std::max(startMs, windowStartMs_);
    clipEndMs_ = std::min(endMs, windowEndMs_);
    if (clipEndMs_ < clipStartMs_ + minDurationMs) {
        clipEndMs_ = std::min(windowEndMs_, clipStartMs_ + minDurationMs);
        if (clipEndMs_ < clipStartMs_ + minDurationMs) {
            clipStartMs_ = std::max(windowStartMs_, clipEndMs_ - minDurationMs);
        }
    }

    playheadMs_ = clipStartMs_;
    dragTarget_ = DragTarget::None;
    update();
}

void ClipTrimBar::setPlayheadMs(qint64 playheadMs) {
    if (playheadMs_ == playheadMs) return;
    playheadMs_ = playheadMs;
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

QRect ClipTrimBar::startHandleRect() const {
    const int x = msToX(clipStartMs_);
    return {x - kHandleWidth / 2, 0, kHandleWidth, kTrackHeight};
}

QRect ClipTrimBar::endHandleRect() const {
    const int x = msToX(clipEndMs_);
    return {x - kHandleWidth / 2, 0, kHandleWidth, kTrackHeight};
}

qint64 ClipTrimBar::effectiveMinClipDurationMs() const {
    const qint64 windowDurationMs = windowEndMs_ - windowStartMs_;
    if (windowDurationMs <= 0) return 0;
    return std::min(kMinClipDurationMs, windowDurationMs);
}

qint64 ClipTrimBar::clampedClipStartMs(qint64 proposedStartMs) const {
    const qint64 latestStartMs = clipEndMs_ - effectiveMinClipDurationMs();
    const qint64 lo = windowStartMs_;
    const qint64 hi = latestStartMs;
    // std::clamp requires lo <= hi; a window shorter than the minimum duration (or a
    // start that cannot stay in-window without crossing the end) has no legal range.
    if (hi < lo) return clipStartMs_;
    return std::clamp(proposedStartMs, lo, hi);
}

qint64 ClipTrimBar::clampedClipEndMs(qint64 proposedEndMs) const {
    const qint64 earliestEndMs = clipStartMs_ + effectiveMinClipDurationMs();
    const qint64 lo = earliestEndMs;
    const qint64 hi = windowEndMs_;
    if (lo > hi) return clipEndMs_;
    return std::clamp(proposedEndMs, lo, hi);
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

    const int startX = msToX(clipStartMs_);
    const int endX = msToX(clipEndMs_);

    // Selected range highlight
    painter.setBrush(Style::ThemeColors::playheadHighlight(50));
    painter.drawRect(startX, 0, endX - startX, kTrackHeight);

    // Event-mark marker (thin dashed line)
    const int markX = msToX(markMs_);
    painter.setPen(QPen(QColor(255, 200, 50, 180), 1, Qt::DashLine));
    painter.drawLine(markX, 2, markX, kTrackHeight - 2);

    // Playhead
    if (playheadMs_ >= windowStartMs_ && playheadMs_ <= windowEndMs_) {
        const int phX = msToX(playheadMs_);
        painter.setPen(QPen(QColor(255, 255, 255), 2));
        painter.drawLine(phX, 0, phX, kTrackHeight);
    }

    // Start handle
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(72, 199, 142));
    painter.drawRoundedRect(startHandleRect(), 3, 3);

    // End handle
    painter.setBrush(QColor(248, 113, 113));
    painter.drawRoundedRect(endHandleRect(), 3, 3);

    // Time labels below track
    painter.setPen(QColor(160, 160, 160));
    QFont labelFont = font();
    labelFont.setPointSizeF(9.0);
    painter.setFont(labelFont);

    const int labelY = kTrackHeight + kLabelGap;
    const QFontMetrics fm(labelFont);

    const QString startText = formatMs(clipStartMs_);
    painter.drawText(startX - fm.horizontalAdvance(startText) / 2, labelY,
                     fm.horizontalAdvance(startText) + 2, kLabelRowHeight,
                     Qt::AlignCenter, startText);

    const qint64 durationMs = clipEndMs_ - clipStartMs_;
    const double durationSec = durationMs / 1000.0;
    const QString durText = QStringLiteral("%1s").arg(durationSec, 0, 'f', 1);
    const int durX = (startX + endX) / 2;
    painter.drawText(durX - fm.horizontalAdvance(durText) / 2, labelY,
                     fm.horizontalAdvance(durText) + 2, kLabelRowHeight,
                     Qt::AlignCenter, durText);

    const QString endText = formatMs(clipEndMs_);
    painter.drawText(endX - fm.horizontalAdvance(endText) / 2, labelY,
                     fm.horizontalAdvance(endText) + 2, kLabelRowHeight,
                     Qt::AlignCenter, endText);
}

void ClipTrimBar::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    const QPoint pos = event->pos();
    const int hitTolerance = 6;

    const QRect startRect = startHandleRect().adjusted(-hitTolerance, 0, hitTolerance, 0);
    const QRect endRect = endHandleRect().adjusted(-hitTolerance, 0, hitTolerance, 0);

    if (startRect.contains(pos) && endRect.contains(pos)) {
        const int distanceToStart = std::abs(pos.x() - startHandleRect().center().x());
        const int distanceToEnd = std::abs(pos.x() - endHandleRect().center().x());
        dragTarget_ = (distanceToStart <= distanceToEnd) ? DragTarget::ClipStart : DragTarget::ClipEnd;
    } else if (startRect.contains(pos)) {
        dragTarget_ = DragTarget::ClipStart;
    } else if (endRect.contains(pos)) {
        dragTarget_ = DragTarget::ClipEnd;
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
        const QRect startRect = startHandleRect().adjusted(-hitTolerance, 0, hitTolerance, 0);
        const QRect endRect = endHandleRect().adjusted(-hitTolerance, 0, hitTolerance, 0);
        if (startRect.contains(pos) || endRect.contains(pos)) {
            setCursor(Qt::SizeHorCursor);
        } else {
            setCursor(Qt::ArrowCursor);
        }
        return;
    }

    const qint64 rawMs = xToMs(event->pos().x());

    if (dragTarget_ == DragTarget::ClipStart) {
        const qint64 clamped = clampedClipStartMs(rawMs);
        if (clamped != clipStartMs_) {
            clipStartMs_ = clamped;
            update();
            emit clipStartChanged(clipStartMs_);
            emit seekRequested(clipStartMs_);
        }
    } else if (dragTarget_ == DragTarget::ClipEnd) {
        const qint64 clamped = clampedClipEndMs(rawMs);
        if (clamped != clipEndMs_) {
            clipEndMs_ = clamped;
            update();
            emit clipEndChanged(clipEndMs_);
            emit seekRequested(clipEndMs_);
        }
    }
}

void ClipTrimBar::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        dragTarget_ = DragTarget::None;
    }
    QWidget::mouseReleaseEvent(event);
}
