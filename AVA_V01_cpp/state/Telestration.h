#pragma once

#include <QColor>
#include <QObject>
#include <QPainter>
#include <QRectF>
#include <QSet>
#include <QSize>
#include <QString>
#include <QVector>
#include <QtGlobal>

#include <optional>

class TagSession;

/// In-memory telestration for the open video. Drawings are not written to a
/// session file and are not part of the Sportscode XML export.
namespace Telestration {

/// How long a new drawing stays up. Presentation pauses for this long.
/// Export shows the drawing over moving video for the same span.
inline constexpr qint64 kDefaultHoldMilliseconds = 3000;

/// Stroke width as a fraction of the shorter side of the video rectangle.
inline constexpr double kDefaultStrokeWidth = 0.008;

enum class Tool {
    None,
    Arrow,
    DottedArrow,
    Circle,
    Freehand,
};

enum class ShapeKind {
    Arrow,
    DottedArrow,
    Circle,
    Freehand,
};

enum class InkColor {
    White,
    Black,
    Blue,
    Red,
};

struct Point {
    double x = 0.0;
    double y = 0.0;
};

struct Shape {
    ShapeKind kind = ShapeKind::Arrow;
    InkColor color = InkColor::White;
    QVector<Point> points;
    double strokeWidth = kDefaultStrokeWidth;
};

/// One telestration anchored to a video timestamp. Coordinates are normalized
/// to the video picture (0..1), origin at the top-left, so the same drawing
/// fits any window and any export size. tagId is the stable GameTag::id.
struct Drawing {
    quint64 id = 0;
    std::optional<quint64> tagId;
    qint64 anchorMs = 0;
    qint64 holdMs = kDefaultHoldMilliseconds;
    QVector<Shape> shapes;
};

struct ClipWindow {
    quint64 tagId = 0;
    qint64 startMs = 0;
    qint64 endMs = 0;
};

struct ExportWindow {
    double startOffsetSeconds = 0.0;
    double endOffsetSeconds = 0.0;
};

struct FreezeCue {
    quint64 drawingId = 0;
    qint64 anchorMs = 0;
    qint64 holdMs = 0;
};

QColor colorForInk(InkColor color);

/// Maps a point in \p videoRectangle (the picture, letterboxing excluded) into 0..1.
/// Points outside the rectangle clamp to the edge. An empty rectangle yields (0, 0).
Point normalizedPointFromVideo(const QPointF& videoPoint, const QRectF& videoRectangle);

/// Inverse of normalizedPointFromVideo. Normalized values outside 0..1 clamp.
QPointF videoPointFromNormalized(const Point& point, const QRectF& videoRectangle);

/// True while \p positionMs is inside [anchor, anchor + hold).
bool drawingVisibleOnTimeline(const Drawing& drawing, qint64 positionMs);

/// Tagged drawings belong to that tag's clip. Free-standing drawings belong
/// when their anchor lies inside the clip.
bool drawingBelongsToClip(const Drawing& drawing, const ClipWindow& clip);

/// Intersection of the drawing's display window with the clip, as seconds from
/// the clip start. Empty when the drawing is not on this clip or the windows
/// do not overlap. Export plays this span over moving video.
std::optional<ExportWindow> exportWindowForClip(const Drawing& drawing, const ClipWindow& clip);

/// The earliest drawing whose anchor the playhead just reached while playing
/// continuously. A seek larger than presentation's continuous-advance limit
/// does not freeze. Already-held drawings are skipped.
std::optional<FreezeCue> earliestFreezeCue(const QVector<Drawing>& drawings,
                                           const ClipWindow& clip,
                                           qint64 previousPositionMs,
                                           qint64 positionMs,
                                           bool playing,
                                           const QSet<quint64>& heldDrawingIds);

/// Drawings passed during a freeze (anchor after \p crossedFromMs and at or
/// before \p positionMs) that still need their own hold. Used to chain
/// drawings that share a timestamp.
std::optional<FreezeCue> earliestUnheldCueThrough(const QVector<Drawing>& drawings,
                                                  const ClipWindow& clip,
                                                  qint64 crossedFromMs,
                                                  qint64 positionMs,
                                                  const QSet<quint64>& heldDrawingIds);

/// Drop held ids whose anchor is still ahead of \p positionMs so a rewind can
/// freeze them again.
void forgetHeldDrawingsAfterRewind(QSet<quint64>& heldDrawingIds,
                                   const QVector<Drawing>& drawings,
                                   qint64 positionMs);

/// Tightest tag interval that contains \p positionMs. Equal spans keep the
/// earlier tag. Empty when none contain the position.
std::optional<quint64> tagIdCoveringPosition(const TagSession& session, qint64 positionMs);

/// The only routine that knows how to draw a shape. Screen items and export
/// PNGs both call this.
void paintDrawing(QPainter& painter, const QRectF& videoRectangle, const Drawing& drawing);

/// Full-frame transparent PNG of \p drawing at \p pixelSize. False when the
/// size is empty or the file cannot be written.
bool writeDrawingPng(const Drawing& drawing, const QSize& pixelSize, const QString& filePath);

class Session final : public QObject {
    Q_OBJECT

public:
    explicit Session(QObject* parent = nullptr);

    const QVector<Drawing>& drawings() const { return drawings_; }

    /// Appends \p shape to the last drawing when that drawing is anchored at
    /// \p anchorMs with the same tag. Otherwise starts a new drawing.
    quint64 commitShape(const Shape& shape,
                        qint64 anchorMs,
                        const std::optional<quint64>& tagId,
                        qint64 holdMs = kDefaultHoldMilliseconds);

    /// Removes the last shape. A drawing with no shapes left is removed.
    bool removeLastShape();
    void clear();

signals:
    void drawingsChanged();

private:
    QVector<Drawing> drawings_;
    quint64 nextDrawingId_ = 1;
};

}  // namespace Telestration
