#include "DrawingGraphicsItem.h"

#include <QPainter>
#include <QStyleOptionGraphicsItem>

DrawingGraphicsItem::DrawingGraphicsItem(Telestration::Drawing drawing, QGraphicsItem* parent)
    : QGraphicsItem(parent),
      drawing_(std::move(drawing)) {
    setAcceptedMouseButtons(Qt::NoButton);
    setFlag(QGraphicsItem::ItemIsSelectable, false);
}

void DrawingGraphicsItem::setDrawing(Telestration::Drawing drawing) {
    drawing_ = std::move(drawing);
    update();
}

void DrawingGraphicsItem::noteVideoGeometryChanged() {
    prepareGeometryChange();
    update();
}

void DrawingGraphicsItem::setExplicitRectangle(const QRectF& rectangle) {
    prepareGeometryChange();
    explicitRectangle_ = rectangle;
    update();
}

QRectF DrawingGraphicsItem::boundingRect() const {
    if (const QGraphicsItem* parent = parentItem()) {
        return QRectF(QPointF(0, 0), parent->boundingRect().size());
    }
    return explicitRectangle_;
}

void DrawingGraphicsItem::paint(QPainter* painter,
                                const QStyleOptionGraphicsItem* option,
                                QWidget* widget) {
    Q_UNUSED(option);
    Q_UNUSED(widget);
    if (painter == nullptr) return;
    Telestration::paintDrawing(*painter, boundingRect(), drawing_);
}
