#include "TelestrationSurface.h"

#include "DrawingGraphicsItem.h"
#include "TelestrationToolBar.h"

#include <QCursor>
#include <QEvent>
#include <QGraphicsScene>
#include <QGraphicsVideoItem>
#include <QGraphicsView>
#include <QMediaPlayer>
#include <QMouseEvent>

#include <cmath>

namespace {

constexpr double kMinimumStrokePixels = 4.0;
constexpr double kFreehandSampleNormalizedDistance = 0.004;
constexpr int kToolBarMarginPixels = 10;

Telestration::ShapeKind shapeKindForTool(Telestration::Tool tool) {
    switch (tool) {
    case Telestration::Tool::DottedArrow:
        return Telestration::ShapeKind::DottedArrow;
    case Telestration::Tool::Circle:
        return Telestration::ShapeKind::Circle;
    case Telestration::Tool::Freehand:
        return Telestration::ShapeKind::Freehand;
    case Telestration::Tool::Arrow:
    case Telestration::Tool::None:
        return Telestration::ShapeKind::Arrow;
    }
    return Telestration::ShapeKind::Arrow;
}

double normalizedDistance(const Telestration::Point& start, const Telestration::Point& end) {
    const double deltaX = start.x - end.x;
    const double deltaY = start.y - end.y;
    return std::sqrt((deltaX * deltaX) + (deltaY * deltaY));
}

}  // namespace

TelestrationSurface::TelestrationSurface(QObject* parent)
    : QObject(parent),
      presentationMode_(false),
      forcedDrawingId_(0),
      strokeActive_(false) {}

void TelestrationSurface::attach(QGraphicsView* view,
                                 QGraphicsVideoItem* videoItem,
                                 QMediaPlayer* player) {
    view_ = view;
    videoItem_ = videoItem;
    player_ = player;
    if (view_ != nullptr && view_->viewport() != nullptr && toolBar_ == nullptr) {
        toolBar_ = new TelestrationToolBar(view_->viewport());
        toolBar_->hide();
        connect(toolBar_, &TelestrationToolBar::toolChanged, this, [this](Telestration::Tool) {
            updateToolCursor();
        });
        connect(toolBar_, &TelestrationToolBar::undoRequested, this, [this]() {
            cancelStroke();
            if (session_ != nullptr) session_->removeLastShape();
        });
    }
    if (player_ != nullptr) {
        connect(player_, &QMediaPlayer::positionChanged, this, [this](qint64) {
            refreshVisibility();
        });
        connect(player_, &QMediaPlayer::playbackStateChanged, this,
                [this](QMediaPlayer::PlaybackState) {
                    refreshVisibility();
                    updateToolCursor();
                });
    }
    relayout();
}

void TelestrationSurface::setSession(Telestration::Session* session) {
    if (session_ == session) return;
    if (sessionConnection_) {
        QObject::disconnect(sessionConnection_);
        sessionConnection_ = {};
    }
    session_ = session;
    if (session_ != nullptr) {
        sessionConnection_ = connect(session_, &Telestration::Session::drawingsChanged, this,
                                     &TelestrationSurface::rebuildItems);
    }
    rebuildItems();
}

void TelestrationSurface::setTagResolver(
    std::function<std::optional<quint64>(qint64 positionMs)> resolver) {
    tagResolver_ = std::move(resolver);
}

void TelestrationSurface::setPresentationMode(bool presenting) {
    if (presentationMode_ == presenting) return;
    presentationMode_ = presenting;
    refreshVisibility();
}

void TelestrationSurface::setForcedVisibleDrawingId(quint64 drawingId) {
    if (forcedDrawingId_ == drawingId) return;
    forcedDrawingId_ = drawingId;
    refreshVisibility();
}

void TelestrationSurface::setToolsEnabled(bool enabled) {
    if (toolBar_ != nullptr) toolBar_->setActionsEnabled(enabled);
    if (!enabled) cancelStroke();
    updateToolCursor();
}

void TelestrationSurface::setToolsVisible(bool visible) {
    if (toolBar_ == nullptr) return;
    toolBar_->setVisible(visible);
    if (visible) positionToolBar();
    if (!visible) cancelStroke();
}

void TelestrationSurface::relayout() {
    if (videoItem_ == nullptr || view_ == nullptr || view_->viewport() == nullptr) return;
    const QSize viewportSize = view_->viewport()->size();
    if (viewportSize.width() <= 0 || viewportSize.height() <= 0) return;

    const QRectF viewRectangle(QPointF(0, 0), QSizeF(viewportSize));
    view_->setSceneRect(viewRectangle);

    QSizeF nativeSize = videoItem_->nativeSize();
    if (!nativeSize.isValid() || nativeSize.isEmpty()) nativeSize = viewRectangle.size();
    QSizeF fittedSize = nativeSize;
    fittedSize.scale(viewRectangle.size(), Qt::KeepAspectRatio);
    videoItem_->setOffset(QPointF(0, 0));
    videoItem_->setPos((viewRectangle.width() - fittedSize.width()) / 2.0,
                       (viewRectangle.height() - fittedSize.height()) / 2.0);
    videoItem_->setSize(fittedSize);

    for (DrawingGraphicsItem* item : drawingItems_) {
        if (item != nullptr) item->noteVideoGeometryChanged();
    }
    if (previewItem_ != nullptr) previewItem_->noteVideoGeometryChanged();
    positionToolBar();
    refreshVisibility();
}

bool TelestrationSurface::handleViewportEvent(QObject* watched, QEvent* event) {
    Q_UNUSED(watched);
    if (event == nullptr || toolBar_ == nullptr || videoItem_ == nullptr) return false;
    if (toolBar_->tool() == Telestration::Tool::None) return false;
    if (player_ != nullptr && player_->playbackState() == QMediaPlayer::PlayingState) return false;

    if (event->type() == QEvent::MouseButtonPress) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::RightButton && strokeActive_) {
            cancelStroke();
            return true;
        }
        if (mouseEvent->button() != Qt::LeftButton) return false;
        bool insidePicture = false;
        const std::optional<Telestration::Point> point =
            normalizedPointInPicture(mouseEvent->position().toPoint(), &insidePicture);
        if (!insidePicture || !point.has_value()) return false;
        beginStroke(*point);
        return true;
    }

    if (event->type() == QEvent::MouseMove && strokeActive_) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        bool insidePicture = false;
        const std::optional<Telestration::Point> point =
            normalizedPointInPicture(mouseEvent->position().toPoint(), &insidePicture);
        if (point.has_value()) extendStroke(*point);
        return true;
    }

    if (event->type() == QEvent::MouseButtonRelease && strokeActive_) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() != Qt::LeftButton) return true;
        bool insidePicture = false;
        const std::optional<Telestration::Point> point =
            normalizedPointInPicture(mouseEvent->position().toPoint(), &insidePicture);
        if (point.has_value()) extendStroke(*point);
        finishStroke();
        return true;
    }
    return false;
}

void TelestrationSurface::rebuildItems() {
    for (DrawingGraphicsItem* item : drawingItems_) {
        delete item;
    }
    drawingItems_.clear();
    if (session_ == nullptr || videoItem_ == nullptr) {
        refreshVisibility();
        return;
    }

    int stackingOrder = 1;
    for (const Telestration::Drawing& drawing : session_->drawings()) {
        auto* item = new DrawingGraphicsItem(drawing, videoItem_);
        item->setZValue(stackingOrder++);
        drawingItems_.insert(drawing.id, item);
    }
    refreshVisibility();
}

void TelestrationSurface::refreshVisibility() {
    const qint64 positionMs = player_ != nullptr ? player_->position() : 0;
    const bool playing =
        player_ != nullptr && player_->playbackState() == QMediaPlayer::PlayingState;
    for (auto iterator = drawingItems_.cbegin(); iterator != drawingItems_.cend(); ++iterator) {
        DrawingGraphicsItem* item = iterator.value();
        if (item == nullptr) continue;
        const Telestration::Drawing* drawing = nullptr;
        if (session_ != nullptr) {
            for (const Telestration::Drawing& candidate : session_->drawings()) {
                if (candidate.id == iterator.key()) {
                    drawing = &candidate;
                    item->setDrawing(candidate);
                    break;
                }
            }
        }
        bool visible = false;
        if (forcedDrawingId_ != 0 && iterator.key() == forcedDrawingId_) {
            visible = true;
        } else if (drawing != nullptr) {
            const bool onTimeline = Telestration::drawingVisibleOnTimeline(*drawing, positionMs);
            if (!presentationMode_) {
                visible = onTimeline;
            } else if (!playing) {
                visible = onTimeline;
            }
        }
        item->setVisible(visible);
    }
}

void TelestrationSurface::updateToolCursor() {
    if (view_ == nullptr || view_->viewport() == nullptr || toolBar_ == nullptr) return;
    const bool armed = toolBar_->isEnabled() && toolBar_->isVisible()
                       && toolBar_->tool() != Telestration::Tool::None
                       && (player_ == nullptr
                           || player_->playbackState() != QMediaPlayer::PlayingState);
    view_->viewport()->setCursor(armed ? Qt::CrossCursor : Qt::ArrowCursor);
}

void TelestrationSurface::positionToolBar() {
    if (toolBar_ == nullptr || view_ == nullptr || view_->viewport() == nullptr) return;
    toolBar_->adjustSize();
    const QSize viewportSize = view_->viewport()->size();
    const int x = kToolBarMarginPixels;
    const int availableHeight = viewportSize.height() - toolBar_->height() - (kToolBarMarginPixels * 2);
    const int y = kToolBarMarginPixels + std::max(0, availableHeight / 2);
    toolBar_->move(x, y);
    toolBar_->raise();
}

QRectF TelestrationSurface::videoSceneRectangle() const {
    if (videoItem_ == nullptr) return {};
    return videoItem_->sceneBoundingRect();
}

std::optional<Telestration::Point> TelestrationSurface::normalizedPointInPicture(
    const QPoint& viewportPosition,
    bool* insidePicture) const {
    if (insidePicture != nullptr) *insidePicture = false;
    if (view_ == nullptr || videoItem_ == nullptr) return std::nullopt;
    const QRectF picture = videoSceneRectangle();
    if (picture.width() <= 0.0 || picture.height() <= 0.0) return std::nullopt;
    const QPointF scenePoint = view_->mapToScene(viewportPosition);
    const bool contains = picture.contains(scenePoint);
    if (insidePicture != nullptr) *insidePicture = contains;
    return Telestration::normalizedPointFromVideo(scenePoint, picture);
}

void TelestrationSurface::beginStroke(const Telestration::Point& point) {
    cancelStroke();
    if (videoItem_ == nullptr || toolBar_ == nullptr) return;
    strokeActive_ = true;
    if (view_ != nullptr && view_->viewport() != nullptr) view_->viewport()->grabMouse();
    strokePoints_.clear();
    strokePoints_.append(point);

    Telestration::Drawing preview;
    preview.shapes.append(shapeFromStroke());
    previewItem_ = new DrawingGraphicsItem(preview, videoItem_);
    previewItem_->setZValue(1000);
    previewItem_->setVisible(true);
}

void TelestrationSurface::extendStroke(const Telestration::Point& point) {
    if (!strokeActive_ || toolBar_ == nullptr) return;
    if (toolBar_->tool() == Telestration::Tool::Freehand) {
        if (!strokePoints_.isEmpty()
            && normalizedDistance(strokePoints_.constLast(), point) < kFreehandSampleNormalizedDistance) {
            return;
        }
        strokePoints_.append(point);
    } else if (strokePoints_.isEmpty()) {
        strokePoints_.append(point);
    } else if (strokePoints_.size() == 1) {
        strokePoints_.append(point);
    } else {
        strokePoints_.last() = point;
    }

    if (previewItem_ != nullptr) {
        Telestration::Drawing preview;
        preview.shapes.append(shapeFromStroke());
        previewItem_->setDrawing(preview);
    }
}

void TelestrationSurface::finishStroke() {
    if (!strokeActive_) return;
    const Telestration::Shape shape = shapeFromStroke();
    const bool keep = strokeIsLongEnough();
    const qint64 anchorMs = player_ != nullptr ? player_->position() : 0;
    std::optional<quint64> tagId;
    if (tagResolver_) tagId = tagResolver_(anchorMs);
    cancelStroke();
    if (!keep || session_ == nullptr) return;
    session_->commitShape(shape, anchorMs, tagId);
}

void TelestrationSurface::cancelStroke() {
    if (view_ != nullptr && view_->viewport() != nullptr
        && QWidget::mouseGrabber() == view_->viewport()) {
        view_->viewport()->releaseMouse();
    }
    strokeActive_ = false;
    strokePoints_.clear();
    delete previewItem_;
    previewItem_ = nullptr;
}

Telestration::Shape TelestrationSurface::shapeFromStroke() const {
    Telestration::Shape shape;
    shape.kind = shapeKindForTool(toolBar_ != nullptr ? toolBar_->tool() : Telestration::Tool::Arrow);
    shape.color = toolBar_ != nullptr ? toolBar_->inkColor() : Telestration::InkColor::White;
    shape.strokeWidth = Telestration::kDefaultStrokeWidth;
    shape.points = strokePoints_;
    if (shape.kind != Telestration::ShapeKind::Freehand && shape.points.size() == 1) {
        shape.points.append(shape.points.at(0));
    }
    return shape;
}

bool TelestrationSurface::strokeIsLongEnough() const {
    if (strokePoints_.size() < 2) return false;
    const QRectF picture = videoSceneRectangle();
    if (picture.width() <= 0.0 || picture.height() <= 0.0) return false;
    double pixelLength = 0.0;
    for (int index = 1; index < strokePoints_.size(); ++index) {
        const QPointF start = Telestration::videoPointFromNormalized(strokePoints_.at(index - 1), picture);
        const QPointF end = Telestration::videoPointFromNormalized(strokePoints_.at(index), picture);
        pixelLength += QLineF(start, end).length();
    }
    return pixelLength >= kMinimumStrokePixels;
}
