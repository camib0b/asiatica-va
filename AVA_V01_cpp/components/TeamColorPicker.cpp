#include "TeamColorPicker.h"

#include "../i18n/AppLocale.h"
#include "../style/StyleProps.h"
#include "../style/ThemeColors.h"

#include <QAbstractButton>
#include <QColor>
#include <QColorDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPointer>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

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

double linearChannel(double channel) {
  return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
}

double relativeLuminance(const QColor& color) {
  return 0.2126 * linearChannel(color.redF()) + 0.7152 * linearChannel(color.greenF()) +
         0.0722 * linearChannel(color.blueF());
}

double contrastRatio(const QColor& first, const QColor& second) {
  const double firstLuminance = relativeLuminance(first);
  const double secondLuminance = relativeLuminance(second);
  const double lighter = std::max(firstLuminance, secondLuminance);
  const double darker = std::min(firstLuminance, secondLuminance);
  return (lighter + 0.05) / (darker + 0.05);
}

QColor contrastingSwatchBorder(const QColor& fill) {
  const QColor darkStroke = Style::ThemeColors::ring();
  const QColor lightStroke = fill.lighter(160);
  if (contrastRatio(fill, darkStroke) >= contrastRatio(fill, lightStroke)) {
    return darkStroke;
  }
  return lightStroke;
}

QRectF strokeEllipseRect(const QRectF& bounds, qreal penWidth, qreal extraInset) {
  const qreal inset = extraInset + penWidth / 2.0;
  return bounds.adjusted(inset, inset, -inset, -inset);
}

QString colorToHex(const QColor& color) {
  return color.name(QColor::HexRgb).toUpper();
}

bool isHexDigit(QChar character) {
  const char latin = character.toLatin1();
  return (latin >= '0' && latin <= '9') || (latin >= 'a' && latin <= 'f') ||
         (latin >= 'A' && latin <= 'F');
}

// Exactly 6 hex digits, optional leading '#', stored as #RRGGBB. Do not use
// QColor here: it accepts #rgb, names, and #rgba and would rewrite while typing.
QString normalizeHex(const QString& text) {
  QString digits = text.trimmed();
  if (digits.startsWith(QLatin1Char('#'))) {
    digits.remove(0, 1);
  }
  if (digits.size() != 6) return {};
  for (const QChar digit : digits) {
    if (!isHexDigit(digit)) return {};
  }
  return QLatin1Char('#') + digits.toUpper();
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

    const bool drawOutline = showSelectionRing_ || hasFocus();
    const qreal outlineWidth = showSelectionRing_ ? 2.0 : (hasFocus() ? 1.5 : 0.0);
    const bool emptySwatch = empty_ || !swatchColor_.isValid();
    const qreal fillPenWidth = emptySwatch ? 1.25 : 1.0;
    constexpr qreal antialiasPad = 0.5;

    const QRectF bounds = QRectF(rect());
    qreal fillExtraInset = antialiasPad;
    if (drawOutline) {
      fillExtraInset += outlineWidth + antialiasPad;
    }
    const QRectF fillCircle = strokeEllipseRect(bounds, fillPenWidth, fillExtraInset);

    if (emptySwatch) {
      QPen dash(Style::ThemeColors::faint());
      dash.setStyle(Qt::DashLine);
      dash.setWidthF(fillPenWidth);
      dash.setCapStyle(Qt::RoundCap);
      painter.setPen(dash);
      painter.setBrush(Qt::NoBrush);
      painter.drawEllipse(fillCircle);
    } else {
      QPen fillPen(contrastingSwatchBorder(swatchColor_));
      fillPen.setWidthF(fillPenWidth);
      painter.setPen(fillPen);
      painter.setBrush(swatchColor_);
      painter.drawEllipse(fillCircle);
    }

    if (drawOutline) {
      QPen outlinePen(Style::ThemeColors::ring());
      outlinePen.setWidthF(outlineWidth);
      painter.setPen(outlinePen);
      painter.setBrush(Qt::NoBrush);
      painter.drawEllipse(strokeEllipseRect(bounds, outlineWidth, antialiasPad));
    }
  }

  QSize sizeHint() const override { return QSize(36, 36); }
  QSize minimumSizeHint() const override { return sizeHint(); }

private:
  QColor swatchColor_;
  bool empty_ = true;
  bool showSelectionRing_ = false;
};

// Qt::Popup grabs the mouse. An outside press is delivered here, the popup
// closes, and Qt then replays that press unless WA_NoMouseReplay is set.
// Suppress replay only when the press is on the well so the well does not
// reopen the popup; clicks on other widgets still get the replayed press.
class PalettePopupFrame final : public QFrame {
public:
  PalettePopupFrame(QWidget* parent, QWidget* wellButton)
      : QFrame(parent, Qt::Popup), wellButton_(wellButton) {}

protected:
  void mousePressEvent(QMouseEvent* event) override {
    if (wellButton_) {
      const QPoint wellLocal = wellButton_->mapFromGlobal(event->globalPosition().toPoint());
      if (wellButton_->rect().contains(wellLocal)) {
        setAttribute(Qt::WA_NoMouseReplay);
      }
    }
    QFrame::mousePressEvent(event);
  }

private:
  QPointer<QWidget> wellButton_;
};

}  // namespace

TeamColorPicker::TeamColorPicker(QWidget* parent)
    : QWidget(parent),
      colorHex_(),
      colorDialogTitle_(),
      fallbackPreviewColor_(Qt::gray),
      syncingHexEdit_(false),
      wellButton_(nullptr),
      popup_(nullptr),
      hexEdit_(nullptr),
      moreColorsButton_(nullptr),
      swatchButtons_() {
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

void TeamColorPicker::applyUiStrings() const {
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

  popup_ = new PalettePopupFrame(this, well);
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
  connect(hexEdit_, &QLineEdit::textChanged, this, &TeamColorPicker::onHexTextChanged);
  connect(hexEdit_, &QLineEdit::editingFinished, this, &TeamColorPicker::onHexEditingFinished);
  connect(moreColorsButton_, &QPushButton::clicked, this, &TeamColorPicker::onMoreColorsClicked);
}

void TeamColorPicker::onWellClicked() {
  if (popup_ && popup_->isVisible()) {
    hidePalettePopup();
    return;
  }
  showPalettePopup();
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
  const bool colorValueChanged =
      colorHex_.compare(normalizedHex, Qt::CaseInsensitive) != 0;
  colorHex_ = normalizedHex;
  refreshWell();
  refreshSwatchSelection();
  syncHexEditFromColor();
  if (emitChange && colorValueChanged) {
    emit colorChanged(colorHex_);
  }
}

void TeamColorPicker::refreshWell() const {
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

void TeamColorPicker::refreshSwatchSelection() const {
  for (QAbstractButton* button : swatchButtons_) {
    auto* swatch = static_cast<ColorCircleButton*>(button);
    const QString paletteHex = swatch->property("paletteHex").toString();
    const bool selected =
        !colorHex_.isEmpty() && paletteHex.compare(colorHex_, Qt::CaseInsensitive) == 0;
    swatch->setShowSelectionRing(selected);
  }
}

void TeamColorPicker::syncHexEditFromColor() const {
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
