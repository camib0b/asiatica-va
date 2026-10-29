#include "TelestrationToolBar.h"

#include "../i18n/AppLocale.h"
#include "../i18n/LocaleNotifier.h"
#include "../style/StyleProps.h"

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace {

constexpr int kIconSide = 20;

QIcon toolIcon(Telestration::Tool tool, const QColor& glyph) {
    QPixmap pixmap(kIconSide, kIconSide);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(glyph, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    if (tool == Telestration::Tool::DottedArrow) pen.setStyle(Qt::DotLine);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    if (tool == Telestration::Tool::Arrow || tool == Telestration::Tool::DottedArrow) {
        painter.drawLine(QPointF(3, 16), QPointF(15, 4));
        painter.setPen(Qt::NoPen);
        painter.setBrush(glyph);
        QPolygonF head;
        head << QPointF(16, 3) << QPointF(10, 4) << QPointF(15, 10);
        painter.drawPolygon(head);
    } else if (tool == Telestration::Tool::Circle) {
        painter.drawEllipse(QRectF(3, 3, 14, 14));
    } else if (tool == Telestration::Tool::Freehand) {
        QPainterPath path;
        path.moveTo(3, 15);
        path.cubicTo(QPointF(7, 4), QPointF(12, 16), QPointF(17, 6));
        painter.drawPath(path);
    }
    return QIcon(pixmap);
}

QIcon swatchIcon(Telestration::InkColor color, bool selected) {
    QPixmap pixmap(kIconSide, kIconSide);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor ink = Telestration::colorForInk(color);
    painter.setPen(QPen(selected ? QColor(QStringLiteral("#18181b")) : QColor(QStringLiteral("#d4d4d8")),
                        selected ? 2.0 : 1.0));
    painter.setBrush(ink);
    painter.drawEllipse(QRectF(3, 3, 14, 14));
    return QIcon(pixmap);
}

QPushButton* makeToolButton(QWidget* parent, const QString& objectName) {
    auto* button = new QPushButton(parent);
    button->setObjectName(objectName);
    button->setCheckable(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setCursor(Qt::PointingHandCursor);
    button->setIconSize(QSize(kIconSide, kIconSide));
    Style::setSize(button, "xs");
    Style::setVariant(button, "outline");
    return button;
}

QPushButton* makeSwatchButton(QWidget* parent, const QString& objectName) {
    QPushButton* button = makeToolButton(parent, objectName);
    Style::setRole(button, "swatch");
    return button;
}

constexpr QLatin1StringView kToolbarAnchorSettingsKey("telestration/toolbarAnchor");

int alignedStart(int areaStart, int areaLength, int itemLength, int alignment) {
    if (itemLength >= areaLength) return areaStart;
    if (alignment < 0) return areaStart;
    if (alignment > 0) return areaStart + areaLength - itemLength;
    return areaStart + (areaLength - itemLength) / 2;
}

int clampStart(int desired, int areaStart, int areaLength, int itemLength) {
    if (itemLength >= areaLength) return areaStart;
    const int maximum = areaStart + areaLength - itemLength;
    return std::clamp(desired, areaStart, maximum);
}

QRect insetVideoRectangle(const QRect& videoRectangle, int marginPixels) {
    if (marginPixels > 0 && videoRectangle.width() > marginPixels * 2
        && videoRectangle.height() > marginPixels * 2) {
        return videoRectangle.adjusted(marginPixels, marginPixels, -marginPixels, -marginPixels);
    }
    return videoRectangle;
}

QPoint clampedOrigin(const QPoint& desiredOrigin, const QRect& videoRectangle, const QSize& toolbarSize) {
    return QPoint(clampStart(desiredOrigin.x(), videoRectangle.left(), videoRectangle.width(),
                             toolbarSize.width()),
                  clampStart(desiredOrigin.y(), videoRectangle.top(), videoRectangle.height(),
                             toolbarSize.height()));
}

}  // namespace

namespace TelestrationToolBarPlacement {

QPoint originForAnchor(Anchor anchor,
                       const QRect& videoRectangle,
                       const QSize& toolbarSize,
                       int marginPixels) {
    if (!videoRectangle.isValid() || videoRectangle.isEmpty()) return videoRectangle.topLeft();

    int horizontalAlignment = 0;
    int verticalAlignment = -1;
    switch (anchor) {
    case Anchor::TopLeft:
        horizontalAlignment = -1;
        verticalAlignment = -1;
        break;
    case Anchor::TopCenter:
        horizontalAlignment = 0;
        verticalAlignment = -1;
        break;
    case Anchor::TopRight:
        horizontalAlignment = 1;
        verticalAlignment = -1;
        break;
    case Anchor::MiddleLeft:
        horizontalAlignment = -1;
        verticalAlignment = 0;
        break;
    case Anchor::MiddleRight:
        horizontalAlignment = 1;
        verticalAlignment = 0;
        break;
    case Anchor::BottomLeft:
        horizontalAlignment = -1;
        verticalAlignment = 1;
        break;
    case Anchor::BottomCenter:
        horizontalAlignment = 0;
        verticalAlignment = 1;
        break;
    case Anchor::BottomRight:
        horizontalAlignment = 1;
        verticalAlignment = 1;
        break;
    }

    const QRect inset = insetVideoRectangle(videoRectangle, marginPixels);
    const QPoint seated(alignedStart(inset.left(), inset.width(), toolbarSize.width(),
                                     horizontalAlignment),
                        alignedStart(inset.top(), inset.height(), toolbarSize.height(),
                                     verticalAlignment));
    return clampedOrigin(seated, videoRectangle, toolbarSize);
}

Anchor nearestAnchor(const QPoint& desiredOrigin,
                     const QRect& videoRectangle,
                     const QSize& toolbarSize,
                     int marginPixels) {
    constexpr Anchor anchors[] = {
        Anchor::TopLeft,    Anchor::TopCenter,  Anchor::TopRight,   Anchor::MiddleLeft,
        Anchor::MiddleRight, Anchor::BottomLeft, Anchor::BottomCenter, Anchor::BottomRight,
    };
    Anchor bestAnchor = Anchor::TopLeft;
    qint64 bestDistance = std::numeric_limits<qint64>::max();
    for (const Anchor candidate : anchors) {
        const QPoint origin = originForAnchor(candidate, videoRectangle, toolbarSize, marginPixels);
        const qint64 deltaX = qint64(origin.x()) - qint64(desiredOrigin.x());
        const qint64 deltaY = qint64(origin.y()) - qint64(desiredOrigin.y());
        const qint64 distance = (deltaX * deltaX) + (deltaY * deltaY);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestAnchor = candidate;
        }
    }
    return bestAnchor;
}

Anchor storedAnchor() {
    QSettings settings;
    bool converted = false;
    const int storedValue = settings.value(kToolbarAnchorSettingsKey,
                                           int(Anchor::MiddleLeft)).toInt(&converted);
    const int firstAnchor = int(Anchor::TopLeft);
    const int lastAnchor = int(Anchor::BottomRight);
    if (!converted || storedValue < firstAnchor || storedValue > lastAnchor) {
        return Anchor::MiddleLeft;
    }
    return static_cast<Anchor>(storedValue);
}

void storeAnchor(Anchor anchor) {
    QSettings settings;
    settings.setValue(kToolbarAnchorSettingsKey, int(anchor));
    settings.sync();
}

}  // namespace TelestrationToolBarPlacement

TelestrationToolBar::TelestrationToolBar(QWidget* parent)
    : QWidget(parent),
      tool_(Telestration::Tool::None),
      inkColor_(Telestration::InkColor::White),
      anchor_(TelestrationToolBarPlacement::storedAnchor()) {
    setObjectName(QStringLiteral("TelestrationToolBar"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFocusPolicy(Qt::NoFocus);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    auto* toolRow = new QHBoxLayout();
    toolRow->setSpacing(4);
    arrowButton_ = makeToolButton(this, QStringLiteral("TelestrationArrowButton"));
    dottedArrowButton_ = makeToolButton(this, QStringLiteral("TelestrationDottedArrowButton"));
    circleButton_ = makeToolButton(this, QStringLiteral("TelestrationCircleButton"));
    freehandButton_ = makeToolButton(this, QStringLiteral("TelestrationFreehandButton"));
    toolRow->addWidget(arrowButton_);
    toolRow->addWidget(dottedArrowButton_);
    toolRow->addWidget(circleButton_);
    toolRow->addWidget(freehandButton_);
    layout->addLayout(toolRow);

    auto* colorRow = new QHBoxLayout();
    colorRow->setSpacing(4);
    whiteButton_ = makeSwatchButton(this, QStringLiteral("TelestrationWhiteButton"));
    blackButton_ = makeSwatchButton(this, QStringLiteral("TelestrationBlackButton"));
    blueButton_ = makeSwatchButton(this, QStringLiteral("TelestrationBlueButton"));
    redButton_ = makeSwatchButton(this, QStringLiteral("TelestrationRedButton"));
    undoButton_ = new QPushButton(this);
    undoButton_->setObjectName(QStringLiteral("TelestrationUndoButton"));
    undoButton_->setFocusPolicy(Qt::NoFocus);
    undoButton_->setCursor(Qt::PointingHandCursor);
    Style::setSize(undoButton_, "xs");
    Style::setVariant(undoButton_, "ghost");
    colorRow->addWidget(whiteButton_);
    colorRow->addWidget(blackButton_);
    colorRow->addWidget(blueButton_);
    colorRow->addWidget(redButton_);
    colorRow->addWidget(undoButton_);
    layout->addLayout(colorRow);

    connect(arrowButton_, &QPushButton::clicked, this, [this]() {
        selectTool(Telestration::Tool::Arrow);
    });
    connect(dottedArrowButton_, &QPushButton::clicked, this, [this]() {
        selectTool(Telestration::Tool::DottedArrow);
    });
    connect(circleButton_, &QPushButton::clicked, this, [this]() {
        selectTool(Telestration::Tool::Circle);
    });
    connect(freehandButton_, &QPushButton::clicked, this, [this]() {
        selectTool(Telestration::Tool::Freehand);
    });
    connect(whiteButton_, &QPushButton::clicked, this, [this]() {
        selectInkColor(Telestration::InkColor::White);
    });
    connect(blackButton_, &QPushButton::clicked, this, [this]() {
        selectInkColor(Telestration::InkColor::Black);
    });
    connect(blueButton_, &QPushButton::clicked, this, [this]() {
        selectInkColor(Telestration::InkColor::Blue);
    });
    connect(redButton_, &QPushButton::clicked, this, [this]() {
        selectInkColor(Telestration::InkColor::Red);
    });
    connect(undoButton_, &QPushButton::clicked, this, &TelestrationToolBar::undoRequested);
    connect(&LocaleNotifier::instance(), &LocaleNotifier::languageChanged, this,
            &TelestrationToolBar::applyUiStrings);

    whiteButton_->setChecked(true);
    refreshToolIcons();
    refreshSwatchIcons();
    setCursor(Qt::OpenHandCursor);
    installDragFilters();
    applyUiStrings();
}

TelestrationToolBar::~TelestrationToolBar() {
    clearDragCursor();
}

void TelestrationToolBar::setActionsEnabled(bool enabled) {
    setEnabled(enabled);
}

void TelestrationToolBar::applyUiStrings() {
    if (arrowButton_) arrowButton_->setToolTip(AppLocale::trUi("telestration.tool.arrow"));
    if (dottedArrowButton_) {
        dottedArrowButton_->setToolTip(AppLocale::trUi("telestration.tool.dotted_arrow"));
    }
    if (circleButton_) circleButton_->setToolTip(AppLocale::trUi("telestration.tool.circle"));
    if (freehandButton_) freehandButton_->setToolTip(AppLocale::trUi("telestration.tool.freehand"));
    if (whiteButton_) whiteButton_->setToolTip(AppLocale::trUi("telestration.color.white"));
    if (blackButton_) blackButton_->setToolTip(AppLocale::trUi("telestration.color.black"));
    if (blueButton_) blueButton_->setToolTip(AppLocale::trUi("telestration.color.blue"));
    if (redButton_) redButton_->setToolTip(AppLocale::trUi("telestration.color.red"));
    if (undoButton_) {
        undoButton_->setText(AppLocale::trUi("telestration.undo"));
        undoButton_->setToolTip(AppLocale::trUi("telestration.undo"));
    }
    applyPlacement();
}

void TelestrationToolBar::selectTool(Telestration::Tool tool) {
    const Telestration::Tool nextTool = (tool_ == tool) ? Telestration::Tool::None : tool;
    tool_ = nextTool;
    if (arrowButton_) arrowButton_->setChecked(tool_ == Telestration::Tool::Arrow);
    if (dottedArrowButton_) {
        dottedArrowButton_->setChecked(tool_ == Telestration::Tool::DottedArrow);
    }
    if (circleButton_) circleButton_->setChecked(tool_ == Telestration::Tool::Circle);
    if (freehandButton_) freehandButton_->setChecked(tool_ == Telestration::Tool::Freehand);
    refreshToolIcons();
    emit toolChanged(tool_);
}

void TelestrationToolBar::selectInkColor(Telestration::InkColor color) {
    inkColor_ = color;
    if (whiteButton_) whiteButton_->setChecked(color == Telestration::InkColor::White);
    if (blackButton_) blackButton_->setChecked(color == Telestration::InkColor::Black);
    if (blueButton_) blueButton_->setChecked(color == Telestration::InkColor::Blue);
    if (redButton_) redButton_->setChecked(color == Telestration::InkColor::Red);
    refreshSwatchIcons();
    emit inkColorChanged(inkColor_);
}

void TelestrationToolBar::refreshToolIcons() {
    const auto assign = [](QPushButton* button, Telestration::Tool tool) {
        if (button == nullptr) return;
        const QColor glyph = button->isChecked() ? QColor(QStringLiteral("#fafafa"))
                                                  : QColor(QStringLiteral("#18181b"));
        button->setIcon(toolIcon(tool, glyph));
    };
    assign(arrowButton_, Telestration::Tool::Arrow);
    assign(dottedArrowButton_, Telestration::Tool::DottedArrow);
    assign(circleButton_, Telestration::Tool::Circle);
    assign(freehandButton_, Telestration::Tool::Freehand);
}

void TelestrationToolBar::refreshSwatchIcons() {
    const auto assign = [](QPushButton* button, Telestration::InkColor color) {
        if (button == nullptr) return;
        button->setIcon(swatchIcon(color, button->isChecked()));
    };
    assign(whiteButton_, Telestration::InkColor::White);
    assign(blackButton_, Telestration::InkColor::Black);
    assign(blueButton_, Telestration::InkColor::Blue);
    assign(redButton_, Telestration::InkColor::Red);
}

void TelestrationToolBar::setVideoRectangle(const QRect& videoRectangle) {
    if (videoRectangle_ == videoRectangle) {
        if (!pressActive_) applyPlacement();
        return;
    }
    videoRectangle_ = videoRectangle;
    if (pressActive_) cancelDrag();
    else applyPlacement();
}

void TelestrationToolBar::setAnchor(TelestrationToolBarPlacement::Anchor anchor, bool remember) {
    anchor_ = anchor;
    if (remember) TelestrationToolBarPlacement::storeAnchor(anchor_);
    if (pressActive_) cancelDrag();
    else applyPlacement();
}

void TelestrationToolBar::mousePressEvent(QMouseEvent* event) {
    if (event != nullptr && event->button() == Qt::LeftButton) {
        beginPress(event, nullptr);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void TelestrationToolBar::mouseMoveEvent(QMouseEvent* event) {
    if (pressActive_ && pressedButton_ == nullptr && event != nullptr) {
        advanceDrag(event->globalPosition());
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void TelestrationToolBar::mouseReleaseEvent(QMouseEvent* event) {
    if (pressActive_ && pressedButton_ == nullptr && event != nullptr
        && event->button() == Qt::LeftButton) {
        const bool dragged = dragStarted_;
        finishPress(dragged);
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void TelestrationToolBar::hideEvent(QHideEvent* event) {
    cancelDrag();
    QWidget::hideEvent(event);
}

bool TelestrationToolBar::eventFilter(QObject* watched, QEvent* event) {
    auto* button = qobject_cast<QPushButton*>(watched);
    if (button == nullptr || event == nullptr) return QWidget::eventFilter(watched, event);

    if (event->type() == QEvent::MouseButtonPress) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) beginPress(mouseEvent, button);
        return false;
    }
    if (event->type() == QEvent::MouseMove && pressActive_ && pressedButton_ == button) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (advanceDrag(mouseEvent->globalPosition())) return true;
        return false;
    }
    if (event->type() == QEvent::MouseButtonRelease && pressActive_ && pressedButton_ == button) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() != Qt::LeftButton) return false;
        const bool swallowRelease = dragStarted_;
        finishPress(swallowRelease);
        return swallowRelease;
    }
    return QWidget::eventFilter(watched, event);
}

void TelestrationToolBar::installDragFilters() {
    QPushButton* buttons[] = {
        arrowButton_, dottedArrowButton_, circleButton_, freehandButton_,
        whiteButton_, blackButton_, blueButton_, redButton_, undoButton_,
    };
    for (QPushButton* button : buttons) {
        if (button != nullptr) button->installEventFilter(this);
    }
}

void TelestrationToolBar::beginPress(const QMouseEvent* mouseEvent, QPushButton* button) {
    if (!isEnabled() || mouseEvent == nullptr || mouseEvent->button() != Qt::LeftButton) return;
    pressActive_ = true;
    dragStarted_ = false;
    pressGlobal_ = mouseEvent->globalPosition();
    pressToolbarPosition_ = pos();
    pressedButton_ = button;
}

bool TelestrationToolBar::advanceDrag(const QPointF& globalPosition) {
    if (!pressActive_) return false;
    const QPoint delta = (globalPosition - pressGlobal_).toPoint();
    if (!dragStarted_) {
        if (delta.manhattanLength() < QApplication::startDragDistance()) return false;
        dragStarted_ = true;
        if (pressedButton_ != nullptr) pressedButton_->setDown(false);
        if (!dragCursorActive_) {
            QApplication::setOverrideCursor(Qt::ClosedHandCursor);
            dragCursorActive_ = true;
        }
    }
    move(clampedToolbarOrigin(pressToolbarPosition_ + delta));
    return true;
}

void TelestrationToolBar::finishPress(bool dragged) {
    pressActive_ = false;
    dragStarted_ = false;
    pressedButton_ = nullptr;
    clearDragCursor();
    if (dragged) snapToNearestAnchor(true);
}

void TelestrationToolBar::cancelDrag() {
    if (!pressActive_ && !dragCursorActive_) return;
    if (pressedButton_ != nullptr) pressedButton_->setDown(false);
    pressActive_ = false;
    dragStarted_ = false;
    pressedButton_ = nullptr;
    clearDragCursor();
    applyPlacement();
}

void TelestrationToolBar::clearDragCursor() {
    if (!dragCursorActive_) return;
    QApplication::restoreOverrideCursor();
    dragCursorActive_ = false;
}

void TelestrationToolBar::applyPlacement() {
    adjustSize();
    if (!videoRectangle_.isValid() || videoRectangle_.isEmpty()) return;
    move(TelestrationToolBarPlacement::originForAnchor(
        anchor_, videoRectangle_, size(), TelestrationToolBarPlacement::kMarginPixels));
}

void TelestrationToolBar::snapToNearestAnchor(bool remember) {
    if (videoRectangle_.isValid() && !videoRectangle_.isEmpty()) {
        anchor_ = TelestrationToolBarPlacement::nearestAnchor(
            pos(), videoRectangle_, size(), TelestrationToolBarPlacement::kMarginPixels);
    }
    if (remember) TelestrationToolBarPlacement::storeAnchor(anchor_);
    applyPlacement();
}

QPoint TelestrationToolBar::clampedToolbarOrigin(const QPoint& desiredOrigin) const {
    if (!videoRectangle_.isValid() || videoRectangle_.isEmpty()) return desiredOrigin;
    return clampedOrigin(desiredOrigin, videoRectangle_, size());
}
