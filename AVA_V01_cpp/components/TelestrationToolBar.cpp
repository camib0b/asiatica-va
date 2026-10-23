#include "TelestrationToolBar.h"

#include "../i18n/AppLocale.h"
#include "../i18n/LocaleNotifier.h"
#include "../style/StyleProps.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPushButton>
#include <QVBoxLayout>

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

}  // namespace

TelestrationToolBar::TelestrationToolBar(QWidget* parent)
    : QWidget(parent),
      tool_(Telestration::Tool::None),
      inkColor_(Telestration::InkColor::White) {
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
    applyUiStrings();
    adjustSize();
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
    adjustSize();
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
