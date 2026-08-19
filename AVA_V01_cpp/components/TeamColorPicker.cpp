#include "TeamColorPicker.h"

#include "../i18n/AppLocale.h"
#include "../style/StyleProps.h"

#include <QAbstractButton>
#include <QColor>
#include <QColorDialog>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QSizePolicy>
#include <QTimer>
#include <QVBoxLayout>

namespace {

struct PaletteEntry {
  const char* nameKey;
  const char* hex;
};

constexpr PaletteEntry kPalette[] = {
    {"setup.color_red", "#DC2626"},
    {"setup.color_light_blue", "#38BDF8"},
    {"setup.color_dark_blue", "#1E3A8A"},
    {"setup.color_yellow", "#EAB308"},
    {"setup.color_gray", "#9CA3AF"},
    {"setup.color_brown", "#92400E"},
    {"setup.color_white", "#FFFFFF"},
    {"setup.color_black", "#18181B"},
    {"setup.color_green", "#16A34A"},
    {"setup.color_pink", "#EC4899"},
};

bool colorLooksLight(const QColor& color) {
  const double luminance =
      (0.299 * color.redF()) + (0.587 * color.greenF()) + (0.114 * color.blueF());
  return luminance > 0.65;
}

class ColorCircleButton final : public QAbstractButton {
public:
  explicit ColorCircleButton(QWidget* parent = nullptr) : QAbstractButton(parent) {
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
    setCheckable(false);
  }

  void setSwatchColor(const QColor& color) {
    swatchColor_ = color;
    update();
  }

  void setEmpty(bool empty) {
    empty_ = empty;
    update();
  }

  void setShowSelectionRing(bool show) {
    showSelectionRing_ = show;
    update();
  }

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const int ringPad = showSelectionRing_ ? 3 : 2;
    const QRectF circle = QRectF(rect()).adjusted(ringPad, ringPad, -ringPad, -ringPad);

    if (empty_ || !swatchColor_.isValid()) {
      QPen dash(QColor(QStringLiteral("#a1a1aa")));
      dash.setStyle(Qt::DashLine);
      dash.setWidthF(1.25);
      painter.setPen(dash);
      painter.setBrush(Qt::NoBrush);
      painter.drawEllipse(circle);
    } else {
      const QColor border = colorLooksLight(swatchColor_) ? QColor(QStringLiteral("#d4d4d8"))
                                                          : swatchColor_.darker(115);
      painter.setPen(QPen(border, 1));
      painter.setBrush(swatchColor_);
      painter.drawEllipse(circle);
    }

    if (showSelectionRing_ || hasFocus()) {
      painter.setPen(QPen(QColor(QStringLiteral("#18181b")), showSelectionRing_ ? 2 : 1.5));
      painter.setBrush(Qt::NoBrush);
      painter.drawEllipse(QRectF(rect()).adjusted(1, 1, -1, -1));
    }
  }

  QSize sizeHint() const override { return QSize(36, 36); }
  QSize minimumSizeHint() const override { return sizeHint(); }

private:
  QColor swatchColor_;
  bool empty_ = true;
  bool showSelectionRing_ = false;
};

}  // namespace

TeamColorPicker::TeamColorPicker(QWidget* parent) : QWidget(parent) {
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  setFixedSize(36, 36);
  buildUi();
  applyUiStrings();
}

void TeamColorPicker::setColor(const QString& hex) {
  const QString normalized = normalizeHex(hex);
  applyNormalizedColor(normalized, false);
}

void TeamColorPicker::setColorDialogTitle(const QString& title) {
  colorDialogTitle_ = title;
}

void TeamColorPicker::setFallbackPreviewColor(const QColor& color) {
  fallbackPreviewColor_ = color.isValid() ? color : QColor(Qt::gray);
}

void TeamColorPicker::applyUiStrings() {
  if (hexEdit_) {
    hexEdit_->setPlaceholderText(AppLocale::trUi("setup.placeholder_hex"));
  }
  if (moreColorsButton_) {
    moreColorsButton_->setText(AppLocale::trUi("setup.color_more"));
  }
  if (popup_) {
    if (auto* codeLabel = popup_->findChild<QLabel*>(QStringLiteral("TeamColorCodeLabel"))) {
      codeLabel->setText(AppLocale::trUi("setup.color_code"));
    }
  }

  for (QAbstractButton* swatch : swatchButtons_) {
    const QByteArray nameKey = swatch->property("nameKey").toString().toUtf8();
    if (!nameKey.isEmpty()) {
      swatch->setToolTip(AppLocale::trUi(nameKey.constData()));
    }
  }
  refreshWell();
}

void TeamColorPicker::buildUi() {
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  auto* well = new ColorCircleButton(this);
  well->setObjectName(QStringLiteral("TeamColorWell"));
  well->setFixedSize(36, 36);
  well->setEmpty(true);
  wellButton_ = well;
  layout->addWidget(well);
  setFocusProxy(well);

  popup_ = new QFrame(this, Qt::Popup);
  popup_->setObjectName(QStringLiteral("TeamColorPopup"));
  popup_->setAttribute(Qt::WA_StyledBackground, true);
  popup_->setFrameShape(QFrame::NoFrame);
  popup_->setFocusPolicy(Qt::NoFocus);

  auto* popupLayout = new QVBoxLayout(popup_);
  popupLayout->setContentsMargins(12, 12, 12, 12);
  popupLayout->setSpacing(10);

  auto* swatchGrid = new QGridLayout();
  swatchGrid->setContentsMargins(0, 0, 0, 0);
  swatchGrid->setHorizontalSpacing(8);
  swatchGrid->setVerticalSpacing(8);

  int paletteIndex = 0;
  for (const PaletteEntry& entry : kPalette) {
    auto* swatch = new ColorCircleButton(popup_);
    swatch->setFixedSize(32, 32);
    swatch->setSwatchColor(QColor(QLatin1String(entry.hex)));
    swatch->setEmpty(false);
    swatch->setProperty("paletteHex", QString::fromLatin1(entry.hex));
    swatch->setProperty("nameKey", QString::fromLatin1(entry.nameKey));
    swatch->setFocusPolicy(Qt::NoFocus);
    const int row = paletteIndex / 5;
    const int column = paletteIndex % 5;
    swatchGrid->addWidget(swatch, row, column);
    swatchButtons_.append(swatch);
    connect(swatch, &QAbstractButton::clicked, this, [this, hex = QString::fromLatin1(entry.hex)]() {
      applyNormalizedColor(normalizeHex(hex), true);
      hidePalettePopup();
    });
    ++paletteIndex;
  }
  popupLayout->addLayout(swatchGrid);

  auto* hexRow = new QHBoxLayout();
  hexRow->setContentsMargins(0, 0, 0, 0);
  hexRow->setSpacing(8);
  auto* codeLabel = new QLabel(popup_);
  codeLabel->setObjectName(QStringLiteral("TeamColorCodeLabel"));
  Style::setRole(codeLabel, "faint");
  hexEdit_ = new QLineEdit(popup_);
  hexEdit_->setObjectName(QStringLiteral("TeamColorHexEdit"));
  hexEdit_->setMaxLength(7);
  hexRow->addWidget(codeLabel, 0);
  hexRow->addWidget(hexEdit_, 1);
  popupLayout->addLayout(hexRow);

  moreColorsButton_ = new QPushButton(popup_);
  moreColorsButton_->setCursor(Qt::PointingHandCursor);
  moreColorsButton_->setAutoDefault(false);
  moreColorsButton_->setDefault(false);
  moreColorsButton_->setFocusPolicy(Qt::NoFocus);
  Style::setVariant(moreColorsButton_, "ghost");
  Style::setSize(moreColorsButton_, "xs");
  popupLayout->addWidget(moreColorsButton_, 0, Qt::AlignLeft);

  connect(wellButton_, &QAbstractButton::clicked, this, &TeamColorPicker::onWellClicked);
  popup_->installEventFilter(this);
  connect(hexEdit_, &QLineEdit::textChanged, this, &TeamColorPicker::onHexTextChanged);
  connect(hexEdit_, &QLineEdit::editingFinished, this, &TeamColorPicker::onHexEditingFinished);
  connect(moreColorsButton_, &QPushButton::clicked, this, &TeamColorPicker::onMoreColorsClicked);
}

bool TeamColorPicker::eventFilter(QObject* watched, QEvent* event) {
  if (watched == popup_ && event && event->type() == QEvent::Hide) {
    onPopupHide();
  }
  return QWidget::eventFilter(watched, event);
}

void TeamColorPicker::onWellClicked() {
  if (popupJustClosed_) {
    return;
  }
  if (popup_ && popup_->isVisible()) {
    hidePalettePopup();
    return;
  }
  showPalettePopup();
}

void TeamColorPicker::onPopupHide() {
  popupJustClosed_ = true;
  QTimer::singleShot(0, this, [this]() { popupJustClosed_ = false; });
}

void TeamColorPicker::showPalettePopup() {
  if (!popup_ || !wellButton_) return;
  syncHexEditFromColor();
  refreshSwatchSelection();
  popup_->adjustSize();
  const QPoint belowWell = wellButton_->mapToGlobal(QPoint(0, wellButton_->height() + 6));
  popup_->move(belowWell);
  popup_->show();
  popup_->raise();
}

void TeamColorPicker::hidePalettePopup() {
  if (popup_ && popup_->isVisible()) {
    popup_->hide();
  }
}

void TeamColorPicker::onHexTextChanged(const QString& text) {
  if (syncingHexEdit_) return;
  QString digits = text.trimmed();
  if (digits.startsWith(QLatin1Char('#'))) {
    digits.remove(0, 1);
  }
  if (digits.size() != 6) return;
  const QString normalized = normalizeHex(text);
  if (normalized.isEmpty()) return;
  applyNormalizedColor(normalized, true);
}

void TeamColorPicker::onHexEditingFinished() {
  if (!hexEdit_) return;
  const QString normalized = normalizeHex(hexEdit_->text());
  if (normalized.isEmpty()) {
    syncHexEditFromColor();
    return;
  }
  applyNormalizedColor(normalized, true);
}

void TeamColorPicker::onMoreColorsClicked() {
  hidePalettePopup();
  const QColor chosen = QColorDialog::getColor(dialogSeedColor(), window(), colorDialogTitle_);
  if (!chosen.isValid()) return;
  applyNormalizedColor(colorToHex(chosen), true);
}

void TeamColorPicker::applyNormalizedColor(const QString& normalizedHex, bool emitChange) {
  if (colorHex_.compare(normalizedHex, Qt::CaseInsensitive) == 0) {
    refreshWell();
    refreshSwatchSelection();
    return;
  }
  colorHex_ = normalizedHex;
  refreshWell();
  refreshSwatchSelection();
  syncHexEditFromColor();
  if (emitChange) {
    emit colorChanged(colorHex_);
  }
}

void TeamColorPicker::refreshWell() {
  auto* well = static_cast<ColorCircleButton*>(wellButton_);
  if (!well) return;

  if (colorHex_.isEmpty()) {
    well->setEmpty(true);
    well->setSwatchColor(QColor());
    well->setToolTip(AppLocale::trUi("setup.color_choose"));
    return;
  }

  const QColor fill(colorHex_);
  well->setEmpty(false);
  well->setSwatchColor(fill.isValid() ? fill : QColor());

  QString tooltip = colorHex_;
  for (QAbstractButton* swatch : swatchButtons_) {
    const QString paletteHex = swatch->property("paletteHex").toString();
    if (paletteHex.compare(colorHex_, Qt::CaseInsensitive) == 0) {
      const QByteArray nameKey = swatch->property("nameKey").toString().toUtf8();
      tooltip = AppLocale::trUi(nameKey.constData());
      break;
    }
  }
  well->setToolTip(tooltip);
}

void TeamColorPicker::refreshSwatchSelection() {
  for (QAbstractButton* button : swatchButtons_) {
    auto* swatch = static_cast<ColorCircleButton*>(button);
    const QString paletteHex = swatch->property("paletteHex").toString();
    const bool selected =
        !colorHex_.isEmpty() && paletteHex.compare(colorHex_, Qt::CaseInsensitive) == 0;
    swatch->setShowSelectionRing(selected);
  }
}

void TeamColorPicker::syncHexEditFromColor() {
  if (!hexEdit_) return;
  syncingHexEdit_ = true;
  hexEdit_->setText(colorHex_);
  syncingHexEdit_ = false;
}

QColor TeamColorPicker::dialogSeedColor() const {
  if (!colorHex_.isEmpty()) {
    const QColor current(colorHex_);
    if (current.isValid()) return current;
  }
  return fallbackPreviewColor_;
}

QString TeamColorPicker::colorToHex(const QColor& color) {
  return QString("#%1%2%3")
      .arg(color.red(), 2, 16, QChar('0'))
      .arg(color.green(), 2, 16, QChar('0'))
      .arg(color.blue(), 2, 16, QChar('0'));
}

QString TeamColorPicker::normalizeHex(const QString& text) {
  QString trimmed = text.trimmed();
  if (trimmed.isEmpty()) return {};
  QColor parsed(trimmed);
  if (!parsed.isValid() && !trimmed.startsWith(QLatin1Char('#'))) {
    parsed = QColor(QLatin1Char('#') + trimmed);
  }
  if (!parsed.isValid()) return {};
  return colorToHex(parsed);
}
