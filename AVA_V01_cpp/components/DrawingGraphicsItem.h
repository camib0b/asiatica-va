#pragma once

#include "../state/Telestration.h"

#include <QGraphicsItem>

/// One drawing on the video scene. paint() delegates to Telestration::paintDrawing
/// so the screen and the export PNG share a renderer.
class DrawingGraphicsItem final : public QGraphicsItem {
public:
    explicit DrawingGraphicsItem(Telestration::Drawing drawing, QGraphicsItem* parent = nullptr);

    void setDrawing(Telestration::Drawing drawing);
    void noteVideoGeometryChanged();
    /// Used when the item has no video parent (tests). Ignored while parented.
    void setExplicitRectangle(const QRectF& rectangle);

    QRectF boundingRect() const override;
    void paint(QPainter* painter,
               const QStyleOptionGraphicsItem* option,
               QWidget* widget = nullptr) override;

private:
    Telestration::Drawing drawing_;
    QRectF explicitRectangle_;
};
