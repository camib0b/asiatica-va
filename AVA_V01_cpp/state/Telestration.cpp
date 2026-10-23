#include "Telestration.h"

#include "PresentationPlayback.h"
#include "TagSession.h"

#include <QImage>
#include <QLineF>
#include <QPainterPath>
#include <QPen>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Telestration {
namespace {

constexpr double kPi = 3.14159265358979323846;

double clampUnitInterval(double value) {
    if (value < 0.0) return 0.0;
    if (value > 1.0) return 1.0;
    return value;
}

bool rectangleIsPaintable(const QRectF& rectangle) {
    return rectangle.width() > 0.0 && rectangle.height() > 0.0;
}

const Drawing* earlierDrawing(const Drawing* current, const Drawing& candidate) {
    if (current == nullptr) return &candidate;
    if (candidate.anchorMs < current->anchorMs) return &candidate;
    if (candidate.anchorMs == current->anchorMs && candidate.id < current->id) return &candidate;
    return current;
}

FreezeCue cueFromDrawing(const Drawing& drawing) {
    FreezeCue cue;
    cue.drawingId = drawing.id;
    cue.anchorMs = drawing.anchorMs;
    cue.holdMs = drawing.holdMs;
    return cue;
}

QColor haloForInk(InkColor color) {
    if (color == InkColor::White) return QColor(0, 0, 0);
    return QColor(255, 255, 255);
}

void paintArrowHead(QPainter& painter,
                    const QPointF& start,
                    const QPointF& end,
                    double strokeWidth,
                    const QColor& color) {
    const double shaftLength = QLineF(start, end).length();
    if (shaftLength < 1.0) return;

    const double headLength = std::max(strokeWidth * 4.5, 8.0);
    const double headHalfWidth = headLength * 0.35;
    const double angle = std::atan2(end.y() - start.y(), end.x() - start.x());
    const QPointF back(end.x() - headLength * std::cos(angle),
                       end.y() - headLength * std::sin(angle));
    const double perpendicular = angle + (kPi / 2.0);
    const QPointF left(back.x() + headHalfWidth * std::cos(perpendicular),
                       back.y() + headHalfWidth * std::sin(perpendicular));
    const QPointF right(back.x() - headHalfWidth * std::cos(perpendicular),
                        back.y() - headHalfWidth * std::sin(perpendicular));

    QPolygonF head;
    head << end << left << right;
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawPolygon(head);
    painter.restore();
}

void paintShape(QPainter& painter, const QRectF& videoRectangle, const Shape& shape) {
    if (shape.points.isEmpty() || !rectangleIsPaintable(videoRectangle)) return;

    const auto mapPoint = [&videoRectangle](const Point& point) {
        return videoPointFromNormalized(point, videoRectangle);
    };
    const double shortSide = std::min(videoRectangle.width(), videoRectangle.height());
    const double strokeWidth = std::max(1.0, shape.strokeWidth * shortSide);
    const QColor ink = colorForInk(shape.color);
    const bool dotted = shape.kind == ShapeKind::DottedArrow;
    const Qt::PenStyle penStyle = dotted ? Qt::DotLine : Qt::SolidLine;

    auto strokePen = [&](const QColor& color, double width) {
        QPen pen(color, width, penStyle, Qt::RoundCap, Qt::RoundJoin);
        pen.setCosmetic(false);
        return pen;
    };

    const auto drawBody = [&]() {
        switch (shape.kind) {
        case ShapeKind::Arrow:
        case ShapeKind::DottedArrow: {
            if (shape.points.size() < 2) return;
            painter.drawLine(mapPoint(shape.points.at(0)), mapPoint(shape.points.at(1)));
            break;
        }
        case ShapeKind::Circle: {
            if (shape.points.size() < 2) return;
            const QPointF center = mapPoint(shape.points.at(0));
            const QPointF edge = mapPoint(shape.points.at(1));
            const double radius = QLineF(center, edge).length();
            if (radius < 0.5) return;
            painter.drawEllipse(center, radius, radius);
            break;
        }
        case ShapeKind::Freehand: {
            if (shape.points.size() == 1) {
                painter.drawPoint(mapPoint(shape.points.at(0)));
                return;
            }
            QPainterPath path;
            path.moveTo(mapPoint(shape.points.at(0)));
            for (int index = 1; index < shape.points.size(); ++index) {
                path.lineTo(mapPoint(shape.points.at(index)));
            }
            painter.drawPath(path);
            break;
        }
        }
    };

    painter.setBrush(Qt::NoBrush);
    painter.setPen(strokePen(haloForInk(shape.color), strokeWidth + 2.0));
    drawBody();
    painter.setPen(strokePen(ink, strokeWidth));
    drawBody();

    if (shape.kind == ShapeKind::Arrow || shape.kind == ShapeKind::DottedArrow) {
        if (shape.points.size() < 2) return;
        const QPointF start = mapPoint(shape.points.at(0));
        const QPointF end = mapPoint(shape.points.at(1));
        paintArrowHead(painter, start, end, strokeWidth + 3.0, haloForInk(shape.color));
        paintArrowHead(painter, start, end, strokeWidth, ink);
    }
}

}  // namespace

QColor colorForInk(InkColor color) {
    switch (color) {
    case InkColor::White:
        return QColor(255, 255, 255);
    case InkColor::Black:
        return QColor(0, 0, 0);
    case InkColor::Blue:
        return QColor(37, 99, 235);
    case InkColor::Red:
        return QColor(220, 38, 38);
    }
    return QColor(255, 255, 255);
}

Point normalizedPointFromVideo(const QPointF& videoPoint, const QRectF& videoRectangle) {
    Point point;
    if (!rectangleIsPaintable(videoRectangle)) return point;
    point.x = clampUnitInterval((videoPoint.x() - videoRectangle.left()) / videoRectangle.width());
    point.y = clampUnitInterval((videoPoint.y() - videoRectangle.top()) / videoRectangle.height());
    return point;
}

QPointF videoPointFromNormalized(const Point& point, const QRectF& videoRectangle) {
    if (!rectangleIsPaintable(videoRectangle)) return videoRectangle.topLeft();
    return QPointF(videoRectangle.left() + clampUnitInterval(point.x) * videoRectangle.width(),
                   videoRectangle.top() + clampUnitInterval(point.y) * videoRectangle.height());
}

bool drawingVisibleOnTimeline(const Drawing& drawing, qint64 positionMs) {
    if (drawing.holdMs <= 0) return false;
    return positionMs >= drawing.anchorMs && positionMs < drawing.anchorMs + drawing.holdMs;
}

bool drawingBelongsToClip(const Drawing& drawing, const ClipWindow& clip) {
    if (drawing.holdMs <= 0) return false;
    if (drawing.tagId.has_value()) return *drawing.tagId == clip.tagId;
    return drawing.anchorMs >= clip.startMs && drawing.anchorMs < clip.endMs;
}

std::optional<ExportWindow> exportWindowForClip(const Drawing& drawing, const ClipWindow& clip) {
    if (drawing.holdMs <= 0) return std::nullopt;
    if (drawing.tagId.has_value() && *drawing.tagId != clip.tagId) return std::nullopt;

    const qint64 windowStart = drawing.anchorMs;
    const qint64 windowEnd = drawing.anchorMs + drawing.holdMs;
    const qint64 visibleStart = std::max(windowStart, clip.startMs);
    const qint64 visibleEnd = std::min(windowEnd, clip.endMs);
    if (visibleEnd <= visibleStart) return std::nullopt;

    ExportWindow window;
    window.startOffsetSeconds = static_cast<double>(visibleStart - clip.startMs) / 1000.0;
    window.endOffsetSeconds = static_cast<double>(visibleEnd - clip.startMs) / 1000.0;
    return window;
}

std::optional<FreezeCue> earliestFreezeCue(const QVector<Drawing>& drawings,
                                           const ClipWindow& clip,
                                           qint64 previousPositionMs,
                                           qint64 positionMs,
                                           bool playing,
                                           const QSet<quint64>& heldDrawingIds) {
    if (!playing) return std::nullopt;
    if (previousPositionMs < 0 || positionMs < previousPositionMs) return std::nullopt;
    if (positionMs - previousPositionMs > PresentationPlayback::kMaxContinuousPlaybackAdvanceMs) {
        return std::nullopt;
    }

    const Drawing* chosen = nullptr;
    for (const Drawing& drawing : drawings) {
        if (!drawingBelongsToClip(drawing, clip)) continue;
        if (heldDrawingIds.contains(drawing.id)) continue;
        if (drawing.anchorMs > positionMs) continue;
        if (previousPositionMs > drawing.anchorMs) continue;
        chosen = earlierDrawing(chosen, drawing);
    }
    if (chosen == nullptr) return std::nullopt;
    return cueFromDrawing(*chosen);
}

std::optional<FreezeCue> earliestUnheldCueThrough(const QVector<Drawing>& drawings,
                                                  const ClipWindow& clip,
                                                  qint64 crossedFromMs,
                                                  qint64 positionMs,
                                                  const QSet<quint64>& heldDrawingIds) {
    const Drawing* chosen = nullptr;
    for (const Drawing& drawing : drawings) {
        if (!drawingBelongsToClip(drawing, clip)) continue;
        if (heldDrawingIds.contains(drawing.id)) continue;
        if (drawing.anchorMs <= crossedFromMs) continue;
        if (drawing.anchorMs > positionMs) continue;
        chosen = earlierDrawing(chosen, drawing);
    }
    if (chosen == nullptr) return std::nullopt;
    return cueFromDrawing(*chosen);
}

void forgetHeldDrawingsAfterRewind(QSet<quint64>& heldDrawingIds,
                                   const QVector<Drawing>& drawings,
                                   qint64 positionMs) {
    QVector<quint64> forgottenIds;
    for (quint64 heldId : heldDrawingIds) {
        for (const Drawing& drawing : drawings) {
            if (drawing.id != heldId) continue;
            if (drawing.anchorMs > positionMs) forgottenIds.append(heldId);
            break;
        }
    }
    for (quint64 forgottenId : forgottenIds) {
        heldDrawingIds.remove(forgottenId);
    }
}

std::optional<quint64> tagIdCoveringPosition(const TagSession& session, qint64 positionMs) {
    const std::optional<quint64> none;
    quint64 bestId = 0;
    qint64 bestSpan = std::numeric_limits<qint64>::max();
    bool found = false;
    for (const TagSession::GameTag& tag : session.tags()) {
        if (positionMs < tag.startMs || positionMs > tag.endMs) continue;
        const qint64 span = tag.endMs - tag.startMs;
        if (found && span >= bestSpan) continue;
        found = true;
        bestSpan = span;
        bestId = tag.id;
    }
    if (!found) return none;
    return bestId;
}

void paintDrawing(QPainter& painter, const QRectF& videoRectangle, const Drawing& drawing) {
    if (!painter.isActive() || !rectangleIsPaintable(videoRectangle)) return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (const Shape& shape : drawing.shapes) {
        paintShape(painter, videoRectangle, shape);
    }
    painter.restore();
}

bool writeDrawingPng(const Drawing& drawing, const QSize& pixelSize, const QString& filePath) {
    if (pixelSize.isEmpty() || pixelSize.width() <= 0 || pixelSize.height() <= 0) return false;
    if (filePath.isEmpty()) return false;

    QImage image(pixelSize, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    if (!painter.isActive()) return false;
    paintDrawing(painter, QRectF(QPointF(0, 0), QSizeF(pixelSize)), drawing);
    painter.end();
    return image.save(filePath, "PNG");
}

Session::Session(QObject* parent)
    : QObject(parent),
      nextDrawingId_(1) {}

quint64 Session::commitShape(const Shape& shape,
                             qint64 anchorMs,
                             const std::optional<quint64>& tagId,
                             qint64 holdMs) {
    const qint64 storedHoldMs = holdMs > 0 ? holdMs : kDefaultHoldMilliseconds;
    if (!drawings_.isEmpty()) {
        Drawing& latest = drawings_.last();
        if (latest.anchorMs == anchorMs && latest.tagId == tagId) {
            latest.shapes.append(shape);
            emit drawingsChanged();
            return latest.id;
        }
    }

    Drawing drawing;
    drawing.id = nextDrawingId_++;
    drawing.tagId = tagId;
    drawing.anchorMs = anchorMs;
    drawing.holdMs = storedHoldMs;
    drawing.shapes.append(shape);
    drawings_.append(drawing);
    emit drawingsChanged();
    return drawing.id;
}

bool Session::removeLastShape() {
    if (drawings_.isEmpty()) return false;
    Drawing& latest = drawings_.last();
    if (!latest.shapes.isEmpty()) latest.shapes.removeLast();
    if (latest.shapes.isEmpty()) drawings_.removeLast();
    emit drawingsChanged();
    return true;
}

void Session::clear() {
    if (drawings_.isEmpty()) return;
    drawings_.clear();
    emit drawingsChanged();
}

}  // namespace Telestration
