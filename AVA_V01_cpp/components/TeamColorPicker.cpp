#include "TeamColorPicker.h"

#include "../i18n/AppLocale.h"
#include "../style/StyleProps.h"
#include "../style/ThemeColors.h"

#include <QAbstractButton>
#include <QApplication>
#include <QColor>
#include <QColorDialog>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
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

constexpr int kPaletteColumnCount = 5;

bool isBackwardTabKey(const QKeyEvent& keyEvent) {
  return keyEvent.key() == Qt::Key_Backtab ||
         (keyEvent.key() == Qt::Key_Tab && keyEvent.modifiers().testFlag(Qt::ShiftModifier));
}

bool isForwardTabKey(const QKeyEvent& keyEvent) {
  return keyEvent.key() == Qt::Key_Tab && !keyEvent.modifiers().testFlag(Qt::ShiftModifier);
}

int neighboringSwatchIndex(int currentIndex, int swatchCount, int key) {
  if (swatchCount <= 0) return currentIndex;
  switch (key) {
    case Qt::Key_Left:
      return (currentIndex + swatchCount - 1) % swatchCount;
    case Qt::Key_Right:
      return (currentIndex + 1) % swatchCount;
    case Qt::Key_Up:
      return (currentIndex - kPaletteColumnCount + swatchCount) % swatchCount;
    case Qt::Key_Down:
      return (currentIndex + kPaletteColumnCount) % swatchCount;
    default:
      return currentIndex;
  }
}

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

// Exactly 6 hex digits, optional leading '#', stored as #RRGGBB. Regex rejects
// shorthand (#rgb), names, and #rgba before QColor runs; partial typing still
// no-ops because the pattern requires all six digits.
QString normalizeHex(const QString& text) {
  const QString trimmed = text.trimmed();
  static const QRegularExpression hexPattern(QStringLiteral("^#?[0-9a-fA-F]{6}$"));
  if (!hexPattern.match(trimmed).hasMatch()) {
    return {};
  }
  const QString withHash =
      trimmed.startsWith(QLatin1Char('#')) ? trimmed : QLatin1Char('#') + trimmed;
  const QColor color(withHash);
  if (!color.isValid()) {
    return {};
  }
  return colorToHex(color);
}

class ColorCircleButton final : public QAbstractButton {
public:
  explicit ColorCircleButton(QWidget* parent = nullptr) : QAbstractButton(parent) {
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
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
  void keyPressEvent(QKeyEvent* event) override {
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && !event->isAutoRepeat()) {
      animateClick();
      event->accept();
      return;
    }
    QAbstractButton::keyPressEvent(event);
  }

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
  setFocusPolicy(Qt::StrongFocus);
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
  popup_->setFocusPolicy(Qt::StrongFocus);

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
    swatch->setFocusPolicy(Qt::StrongFocus);
    const int row = paletteIndex / kPaletteColumnCount;
    const int column = paletteIndex % kPaletteColumnCount;
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
  moreColorsButton_->setFocusPolicy(Qt::StrongFocus);
  Style::setVariant(moreColorsButton_, "ghost");
  Style::setSize(moreColorsButton_, "xs");
  popupLayout->addWidget(moreColorsButton_, 0, Qt::AlignLeft);

  connect(wellButton_, &QAbstractButton::clicked, this, &TeamColorPicker::onWellClicked);
  connect(hexEdit_, &QLineEdit::textChanged, this, &TeamColorPicker::onHexTextChanged);
  connect(hexEdit_, &QLineEdit::editingFinished, this, &TeamColorPicker::onHexEditingFinished);
  connect(moreColorsButton_, &QPushButton::clicked, this, &TeamColorPicker::onMoreColorsClicked);

  popup_->installEventFilter(this);
  hexEdit_->installEventFilter(this);
  moreColorsButton_->installEventFilter(this);
  for (QAbstractButton* swatch : swatchButtons_) {
    swatch->installEventFilter(this);
  }
  const QVector<QWidget*> popupStops = popupKeyboardFocusChain();
  for (int index = 0; index + 1 < popupStops.size(); ++index) {
    setTabOrder(popupStops.at(index), popupStops.at(index + 1));
  }
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

  QWidget* initialFocus = swatchButtons_.isEmpty()
                              ? static_cast<QWidget*>(hexEdit_)
                              : static_cast<QWidget*>(swatchButtons_.constFirst());
  for (QAbstractButton* swatch : swatchButtons_) {
    const QString paletteHex = swatch->property("paletteHex").toString();
    if (!colorHex_.isEmpty() && paletteHex.compare(colorHex_, Qt::CaseInsensitive) == 0) {
      initialFocus = swatch;
      break;
    }
  }
  if (initialFocus) {
    initialFocus->setFocus(Qt::PopupFocusReason);
  }
}

void TeamColorPicker::hidePalettePopup() {
  const bool restoreWellFocus =
      popup_ && popup_->isVisible() &&
      (popup_->hasFocus() || popup_->isAncestorOf(QApplication::focusWidget()));
  if (popup_ && popup_->isVisible()) {
    popup_->hide();
  }
  if (restoreWellFocus && wellButton_) {
    wellButton_->setFocus(Qt::PopupFocusReason);
  }
}

QVector<QWidget*> TeamColorPicker::popupKeyboardFocusChain() const {
  QVector<QWidget*> chain;
  chain.reserve(swatchButtons_.size() + 2);
  for (QAbstractButton* swatch : swatchButtons_) {
    chain.append(swatch);
  }
  if (hexEdit_) chain.append(hexEdit_);
  if (moreColorsButton_) chain.append(moreColorsButton_);
  return chain;
}

bool TeamColorPicker::movePopupKeyboardFocus(bool forward) {
  const QVector<QWidget*> chain = popupKeyboardFocusChain();
  if (chain.isEmpty()) return false;

  QWidget* focus = QApplication::focusWidget();
  int currentIndex = -1;
  for (int index = 0; index < chain.size(); ++index) {
    if (chain.at(index) == focus) {
      currentIndex = index;
      break;
    }
  }

  const int count = chain.size();
  const int nextIndex = currentIndex < 0
                            ? (forward ? 0 : count - 1)
                            : (currentIndex + (forward ? 1 : -1) + count) % count;
  chain.at(nextIndex)->setFocus(forward ? Qt::TabFocusReason : Qt::BacktabFocusReason);
  return true;
}

bool TeamColorPicker::eventFilter(QObject* watched, QEvent* event) {
  if (!event || event->type() != QEvent::KeyPress || !popup_ || !popup_->isVisible()) {
    return QWidget::eventFilter(watched, event);
  }

  auto* keyEvent = static_cast<QKeyEvent*>(event);
  if (keyEvent->key() == Qt::Key_Escape) {
    hidePalettePopup();
    return true;
  }

  auto* watchedButton = qobject_cast<QAbstractButton*>(watched);
  const int swatchIndex = watchedButton ? swatchButtons_.indexOf(watchedButton) : -1;
  if (swatchIndex >= 0) {
    const int key = keyEvent->key();
    if (key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down) {
      const int nextIndex = neighboringSwatchIndex(swatchIndex, swatchButtons_.size(), key);
      swatchButtons_.at(nextIndex)->setFocus(Qt::TabFocusReason);
      return true;
    }
  }

  if (isForwardTabKey(*keyEvent) || isBackwardTabKey(*keyEvent)) {
    return movePopupKeyboardFocus(isForwardTabKey(*keyEvent));
  }

  if (watched == moreColorsButton_ &&
      (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) &&
      !keyEvent->isAutoRepeat()) {
    moreColorsButton_->click();
    return true;
  }

  return QWidget::eventFilter(watched, event);
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
