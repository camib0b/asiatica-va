#pragma once

#include "../state/Telestration.h"

#include <QPoint>
#include <QRect>
#include <QSize>
#include <QWidget>

class QHideEvent;
class QMouseEvent;
class QPushButton;

namespace TelestrationToolBarPlacement {

/// Seats along the video picture. The bar snaps to one and keeps it across resizes.
enum class Anchor {
    TopLeft = 0,
    TopCenter,
    TopRight,
    MiddleLeft,
    MiddleRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
};

inline constexpr int kMarginPixels = 10;

QPoint originForAnchor(Anchor anchor,
                       const QRect& videoRectangle,
                       const QSize& toolbarSize,
                       int marginPixels);

Anchor nearestAnchor(const QPoint& desiredOrigin,
                     const QRect& videoRectangle,
                     const QSize& toolbarSize,
                     int marginPixels);

Anchor storedAnchor();
void storeAnchor(Anchor anchor);

}  // namespace TelestrationToolBarPlacement

/// Tool and ink picker drawn over the video picture.
class TelestrationToolBar final : public QWidget {
    Q_OBJECT

public:
    explicit TelestrationToolBar(QWidget* parent = nullptr);
    ~TelestrationToolBar() override;

    Telestration::Tool tool() const { return tool_; }
    Telestration::InkColor inkColor() const { return inkColor_; }
    void setActionsEnabled(bool enabled);
    void applyUiStrings();

    TelestrationToolBarPlacement::Anchor anchor() const { return anchor_; }
    QRect videoRectangle() const { return videoRectangle_; }
    void setVideoRectangle(const QRect& videoRectangle);
    void setAnchor(TelestrationToolBarPlacement::Anchor anchor, bool remember);

signals:
    void toolChanged(Telestration::Tool tool);
    void inkColorChanged(Telestration::InkColor color);
    void undoRequested();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void selectTool(Telestration::Tool tool);
    void selectInkColor(Telestration::InkColor color);
    void refreshToolIcons();
    void refreshSwatchIcons();
    void installDragFilters();
    void beginPress(const QMouseEvent* mouseEvent, QPushButton* button);
    bool advanceDrag(const QPointF& globalPosition);
    void finishPress(bool dragged);
    void cancelDrag();
    void clearDragCursor();
    void applyPlacement();
    void snapToNearestAnchor(bool remember);
    QPoint clampedToolbarOrigin(const QPoint& desiredOrigin) const;

    Telestration::Tool tool_ = Telestration::Tool::None;
    Telestration::InkColor inkColor_ = Telestration::InkColor::White;
    TelestrationToolBarPlacement::Anchor anchor_ = TelestrationToolBarPlacement::Anchor::MiddleLeft;
    QRect videoRectangle_;
    bool pressActive_ = false;
    bool dragStarted_ = false;
    bool dragCursorActive_ = false;
    QPointF pressGlobal_;
    QPoint pressToolbarPosition_;
    QPushButton* pressedButton_ = nullptr;
    QPushButton* arrowButton_ = nullptr;
    QPushButton* dottedArrowButton_ = nullptr;
    QPushButton* circleButton_ = nullptr;
    QPushButton* freehandButton_ = nullptr;
    QPushButton* undoButton_ = nullptr;
    QPushButton* whiteButton_ = nullptr;
    QPushButton* blackButton_ = nullptr;
    QPushButton* blueButton_ = nullptr;
    QPushButton* redButton_ = nullptr;
};
