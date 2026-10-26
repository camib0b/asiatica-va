#include "GameControls.h"
#include "FollowUpCatalog.h"
#include "../state/EventCatalog.h"
#include "../state/EventControlsValidation.h"
#include "../state/EventLabelResolver.h"
#include "../style/Motion.h"
#include "../style/StyleProps.h"
#include "../style/TextFit.h"
#include "../style/ThemeColors.h"
#include "../i18n/AppLocale.h"
#include "../state/TagSession.h"
#include "../ui/GameControlsContextMenu.h"
#include "../ui/TextInputFocus.h"

#include <QHBoxLayout>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QSpacerItem>
#include <QPushButton>
#include <QWidget>
#include <QTimer>
#include <QPointer>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <algorithm>
#include <array>
#include <climits>
#include <QEvent>
#include <QApplication>
#include <QLabel>
#include <QFont>
#include <QFontMetrics>
#include <QColor>
#include <QSizePolicy>
#include <QLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QStyle>
#include <QStyleOptionButton>

#include "../ui/QtPtr.h"

namespace {
constexpr int kQuarterSegmentHeightPx = 10;
constexpr int kQuarterSegmentRadiusPx = 2;
}

class QuarterSegmentBar final : public QWidget {
  Q_OBJECT
  Q_PROPERTY(qreal fillProgress READ fillProgress WRITE setFillProgress)

 public:
  explicit QuarterSegmentBar(QWidget* parent = nullptr) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(kQuarterSegmentHeightPx);
    setFocusPolicy(Qt::NoFocus);
    fillColor_ = Style::ThemeColors::quarterSegmentEmpty();
  }

  qreal fillProgress() const { return fillProgress_; }

  void setFillProgress(qreal progress) {
    const qreal clamped = qBound(0.0, progress, 1.0);
    if (qAbs(fillProgress_ - clamped) < 0.001) return;
    fillProgress_ = clamped;
    update();
  }

  void setFillColor(const QColor& color) {
    if (fillColor_ == color) return;
    fillColor_ = color;
    update();
  }

  QSize sizeHint() const override { return QSize(24, kQuarterSegmentHeightPx); }
  QSize minimumSizeHint() const override { return QSize(8, kQuarterSegmentHeightPx); }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF bounds = QRectF(rect());
    QPainterPath clipPath;
    clipPath.addRoundedRect(bounds, kQuarterSegmentRadiusPx, kQuarterSegmentRadiusPx);
    painter.setClipPath(clipPath);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Style::ThemeColors::quarterSegmentEmpty());
    painter.drawRect(bounds);
    if (fillProgress_ <= 0.0) return;
    painter.setBrush(fillColor_);
    painter.drawRect(QRectF(bounds.left(), bounds.top(), bounds.width() * fillProgress_, bounds.height()));
  }

 private:
  qreal fillProgress_ = 0.0;
  QColor fillColor_;
};

class QuarterTrackLabel final : public QLabel {
  Q_OBJECT
  Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor)

 public:
  explicit QuarterTrackLabel(const QString& text, QWidget* parent = nullptr) : QLabel(text, parent) {
    setFocusPolicy(Qt::NoFocus);
    textColor_ = Style::ThemeColors::faint();
    applyColor();
  }

  QColor textColor() const { return textColor_; }

  void setTextColor(const QColor& color) {
    if (textColor_ == color) return;
    textColor_ = color;
    applyColor();
  }

 private:
  void applyColor() {
    setStyleSheet(QStringLiteral("QLabel[role=\"quarterTrackLabel\"] { color: %1; }")
                      .arg(textColor_.name(QColor::HexRgb)));
  }

  QColor textColor_;
};

namespace {

constexpr QLatin1StringView kDefaultTeamSelectedBorderHex("#18181b");
constexpr int kFlashDurationMs = 150;

QString normalizeTeamColorHex(const QString& hex) {
  QString hexClean = hex.trimmed();
  if (hexClean.isEmpty()) return {};
  if (!hexClean.startsWith(QLatin1Char('#'))) {
    hexClean.prepend(QLatin1Char('#'));
  }
  const QColor color(hexClean);
  if (!color.isValid()) return {};
  return color.name(QColor::HexRgb);
}

Motion::ButtonColors colorsForGameButton(const QPushButton* button, const QPushButton* activeMainButton) {
  Motion::ButtonColors colors;
  const QString variant = button ? button->property("variant").toString() : QString();
  if (variant == QLatin1String("gameControlFollowUp")) {
    const QColor rest(QStringLiteral("#09090b"));
    const QColor hover(QStringLiteral("#18181b"));
    const QColor pressed(QStringLiteral("#000000"));
    colors.restBackground = rest;
    colors.restBorder = rest;
    colors.hoverBackground = hover;
    colors.hoverBorder = hover;
    colors.pressedBackground = pressed;
    colors.pressedBorder = pressed;
    return colors;
  }

  const bool teamSelected = button && button->property("teamSelected").toBool();
  const bool activeMain = button && button == activeMainButton;
  if (teamSelected || activeMain) {
    const QString borderHex = button->property("teamBorderHex").toString();
    const QColor border = borderHex.isEmpty() ? QColor(kDefaultTeamSelectedBorderHex) : QColor(borderHex);
    colors.restBackground = Style::ThemeColors::hoverSurface();
    colors.hoverBackground = Style::ThemeColors::pressedSurface();
    colors.pressedBackground = QColor(QStringLiteral("#d4d4d8"));
    colors.restBorder = border;
    colors.hoverBorder = border;
    colors.pressedBorder = border;
    colors.keepFocusRing = !teamSelected;
    return colors;
  }

  colors.restBackground = Style::ThemeColors::surface();
  colors.restBorder = Style::ThemeColors::gameControlBorder();
  colors.hoverBackground = Style::ThemeColors::hoverSurface();
  colors.hoverBorder = Style::ThemeColors::gameControlHoverBorder();
  colors.pressedBackground = Style::ThemeColors::pressedSurface();
  colors.pressedBorder = Style::ThemeColors::gameControlHoverBorder();
  return colors;
}

void refreshGameButtonMotion(QPushButton* button, QPushButton* activeMainButton) {
  if (!button) return;
  Motion::setButtonHoverColors(button, colorsForGameButton(button, activeMainButton));
}

void applyTeamButtonSelectionStyle(QPushButton* button, bool selected, const QString& colorHex) {
  if (!button) return;
  Style::setState(button, "teamSelected", selected);
  if (selected) {
    const QString normalized = normalizeTeamColorHex(colorHex);
    button->setProperty("teamBorderHex",
                        normalized.isEmpty() ? QString(kDefaultTeamSelectedBorderHex) : normalized);
  } else {
    button->setProperty("teamBorderHex", QString());
  }
}

EventCatalog::EventId eventIdAtEndOfPath(const QString& mainEvent, const QStringList& selections) {
  const EventCatalog::Node* node = EventCatalog::mainEventForCanonicalName(mainEvent);
  if (!node) return EventCatalog::EventId::None;
  for (const QString& selection : selections) {
    const EventCatalog::Node* child = EventCatalog::childForCanonicalName(*node, selection);
    if (!child) return EventCatalog::EventId::None;
    node = child;
  }
  return node->id;
}

EventLabelOverrides::FollowUpPath followUpPathFor(const QString& mainEvent,
                                                 const QStringList& selections) {
  EventLabelOverrides::FollowUpPath path;
  const EventCatalog::Node* node = EventCatalog::mainEventForCanonicalName(mainEvent);
  if (!node) return path;
  path.mainEventId = node->id;
  for (const QString& selection : selections) {
    if (path.stepCount >= EventCatalog::kMaxFollowUpDepth) return {};
    const EventCatalog::Node* child = EventCatalog::childForCanonicalName(*node, selection);
    if (!child) return {};
    path.steps[static_cast<std::size_t>(path.stepCount)] = child->id;
    ++path.stepCount;
    node = child;
  }
  return path;
}

QString resolvedTitleText(const QLabel* titleLabel, const EventLabelOverrides::Table& overrides) {
  if (!titleLabel) return {};
  const int idValue = titleLabel->property("gameEventId").toInt();
  if (idValue != static_cast<int>(EventCatalog::EventId::None)) {
    return EventLabelResolver::labelForId(static_cast<EventCatalog::EventId>(idValue), overrides);
  }
  return AppLocale::trEvent(titleLabel->property("gameEventName").toString());
}

void configureFollowUpButton(QPushButton* button, const QString& canonicalKey,
                             EventCatalog::EventId eventId, const QString& displayLabel,
                             const QString& shortcutHint) {
  if (!button) return;
  button->setText(QString());
  button->setProperty("gameEventKey", canonicalKey);
  button->setProperty("gameEventId", static_cast<int>(eventId));
  auto* layout = new QVBoxLayout(button);
  layout->setContentsMargins(2, 4, 2, 4);
  layout->setSpacing(2);
  auto* titleLabel = new QLabel(displayLabel, button);
  titleLabel->setProperty("gameEventName", canonicalKey);
  titleLabel->setProperty("gameEventId", static_cast<int>(eventId));
  titleLabel->setProperty("fullLabel", displayLabel);
  titleLabel->setTextFormat(Qt::PlainText);
  titleLabel->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
  titleLabel->setWordWrap(true);
  titleLabel->setMinimumWidth(0);
  titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
  Style::setRole(titleLabel, "gameControlTitle");
  titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  auto* shortcutLabel = new QLabel(shortcutHint, button);
  shortcutLabel->setTextFormat(Qt::PlainText);
  shortcutLabel->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
  shortcutLabel->setMinimumWidth(0);
  shortcutLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  Style::setRole(shortcutLabel, "muted");
  shortcutLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  layout->addWidget(titleLabel);
  layout->addWidget(shortcutLabel);
}

struct FocusNavEntry {
  QPushButton* button = nullptr;
  int row = 0;
  int col = 0;
};

void addKeyboardRow(QVBoxLayout* column, QWidget* parent, const QList<QPushButton*>& buttons,
                    int leftOffsetUnits, int maximumLeftOffsetUnits) {
  if (!column) return;
  auto* row = new QWidget(parent);
  row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  auto* layout = new QHBoxLayout(row);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(4);

  auto* leftSpacer = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Minimum);
  layout->addSpacerItem(leftSpacer);
  layout->setStretch(layout->count() - 1, qMax(0, leftOffsetUnits));

  for (QPushButton* button : buttons) {
    if (!button) continue;
    layout->addWidget(button, EventCatalog::kButtonGridUnitsPerKey);
  }

  const int buttonCount = static_cast<int>(buttons.size());
  const int missingButtons = qMax(0, EventCatalog::kButtonGridColumnCount - buttonCount);
  // The widest row offset is the right edge of a full row. Shorter rows and
  // rows that start further left take the remaining units on the right.
  const int rightOffsetUnits = qMax(0, maximumLeftOffsetUnits - leftOffsetUnits) +
                                (EventCatalog::kButtonGridUnitsPerKey * missingButtons);
  auto* rightSpacer = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Minimum);
  layout->addSpacerItem(rightSpacer);
  layout->setStretch(layout->count() - 1, rightOffsetUnits);

  column->addWidget(row);
}

void publishFittedText(QLabel* titleLabel, const QString& fullText, const Style::FittedButtonTitle& fitted) {
  if (!titleLabel) return;
  if (titleLabel->text() != fitted.shown) titleLabel->setText(fitted.shown);
  const QString tip = fitted.elided ? fullText : QString();
  if (titleLabel->toolTip() != tip) titleLabel->setToolTip(tip);
  if (QWidget* parent = titleLabel->parentWidget()) {
    if (parent->toolTip() != tip) parent->setToolTip(tip);
  }
}

void restoreThemeTitleFont(QLabel* titleLabel) {
  if (!titleLabel) return;
  if (titleLabel->property("titleDensity").toString() != QLatin1String("compact")) return;
  Style::setProp(titleLabel, "titleDensity", QVariant());
  titleLabel->setFont(QFont());
  titleLabel->ensurePolished();
}

QFont themeTitleFont(const QLabel* titleLabel) {
  if (!titleLabel) return {};
  QFont font = titleLabel->font();
  if (titleLabel->property("titleDensity").toString() != QLatin1String("compact")) return font;
  if (font.pixelSize() > 0) {
    font.setPixelSize(font.pixelSize() + 2);
    return font;
  }
  if (font.pointSizeF() > 0) {
    font.setPointSizeF(font.pointSizeF() + 1.0);
  }
  return font;
}

QFont compactTitleFont(const QFont& themeFont) {
  QFont compact = themeFont;
  if (themeFont.pixelSize() > 10) {
    compact.setPixelSize(themeFont.pixelSize() - 2);
    return compact;
  }
  if (themeFont.pointSizeF() > 8.0) {
    compact.setPointSizeF(themeFont.pointSizeF() - 1.0);
    return compact;
  }
  return themeFont;
}

void applyFittedTitle(QLabel* titleLabel, int maximumLines) {
  if (!titleLabel) return;
  const QString fullText = titleLabel->property("fullLabel").toString();
  titleLabel->setTextFormat(Qt::PlainText);
  titleLabel->ensurePolished();

  const int textWidth = titleLabel->contentsRect().width();
  if (textWidth <= 0) {
    Style::FittedButtonTitle pending;
    pending.shown = fullText;
    publishFittedText(titleLabel, fullText, pending);
    return;
  }

  const bool alreadyCompact =
      titleLabel->property("titleDensity").toString() == QLatin1String("compact");
  const QFont themeFont = themeTitleFont(titleLabel);
  const QFont compactFont = compactTitleFont(themeFont);
  const Style::FittedButtonTitle atThemeSize =
      Style::fitButtonTitle(QFontMetrics(themeFont), fullText, textWidth, maximumLines);
  if (!atThemeSize.elided) {
    if (alreadyCompact) restoreThemeTitleFont(titleLabel);
    publishFittedText(titleLabel, fullText, atThemeSize);
    return;
  }

  if (compactFont != themeFont) {
    const Style::FittedButtonTitle atCompactSize =
        Style::fitButtonTitle(QFontMetrics(compactFont), fullText, textWidth, maximumLines);
    if (!atCompactSize.elided) {
      if (!alreadyCompact) Style::setProp(titleLabel, "titleDensity", QStringLiteral("compact"));
      if (titleLabel->font() != compactFont) titleLabel->setFont(compactFont);
      publishFittedText(titleLabel, fullText, atCompactSize);
      return;
    }
  }

  if (alreadyCompact) restoreThemeTitleFont(titleLabel);
  publishFittedText(titleLabel, fullText, atThemeSize);
}

QLabel* titleLabelOnButton(const QPushButton* button) {
  if (!button) return nullptr;
  for (QLabel* label : button->findChildren<QLabel*>(Qt::FindDirectChildrenOnly)) {
    if (!label->property("gameEventName").toString().isEmpty() || label->property("fullLabel").isValid()) {
      return label;
    }
  }
  return nullptr;
}

QLabel* shortcutLabelOnButton(const QPushButton* button) {
  if (!button) return nullptr;
  for (QLabel* label : button->findChildren<QLabel*>(Qt::FindDirectChildrenOnly)) {
    if (label->property("gameEventName").toString().isEmpty() && !label->property("fullLabel").isValid()) {
      return label;
    }
  }
  return nullptr;
}

int measuredTitleChrome(const QPushButton* button) {
  if (!button || button->width() <= 0) return -1;
  const QLabel* titleLabel = titleLabelOnButton(button);
  if (!titleLabel || titleLabel->contentsRect().width() <= 0) return -1;
  return qMax(0, button->width() - titleLabel->contentsRect().width());
}

void elideButtonLabel(QPushButton* button, const QString& fullText) {
  if (!button) return;
  button->ensurePolished();
  const int availableWidth = Style::availableButtonTextWidth(button);
  const Style::FittedButtonTitle fitted =
      Style::fitButtonTitle(button->fontMetrics(), fullText, availableWidth, 1);
  if (button->text() != fitted.shown) button->setText(fitted.shown);
  const QString tip = fitted.elided ? fullText : QString();
  if (button->toolTip() != tip) button->setToolTip(tip);
}

QVector<FocusNavEntry> buildFocusNavEntries(QPushButton* homeTeamButton,
                                            QPushButton* awayTeamButton,
                                            const QList<QPushButton*>& mainButtons,
                                            const QList<QPushButton*>& followUpButtons,
                                            int followUpColumnCount) {
  QVector<FocusNavEntry> entries;
  if (homeTeamButton) {
    entries.append({homeTeamButton, -1, 0});
  }
  if (awayTeamButton) {
    entries.append({awayTeamButton, -1, 1});
  }
  for (int index = 0; index < mainButtons.size(); ++index) {
    QPushButton* button = mainButtons.at(index);
    if (!button) continue;
    const int row = index / EventCatalog::kButtonGridColumnCount;
    const int column = index % EventCatalog::kButtonGridColumnCount;
    entries.append({button, row, column});
  }
  const int columns = qMax(1, followUpColumnCount);
  const int followUpFocusRow = EventCatalog::buttonGridRowCount();
  for (int index = 0; index < followUpButtons.size(); ++index) {
    QPushButton* button = followUpButtons.at(index);
    if (button && button->isVisible()) {
      entries.append({button, followUpFocusRow + index / columns, index % columns});
    }
  }
  return entries;
}

QPushButton* closestButtonInRow(const QVector<FocusNavEntry>& entries, int row, int preferredCol) {
  QPushButton* bestButton = nullptr;
  int bestDistance = INT_MAX;
  for (const FocusNavEntry& entry : entries) {
    if (entry.row != row || !entry.button) {
      continue;
    }
    const int distance = std::abs(entry.col - preferredCol);
    if (distance < bestDistance) {
      bestDistance = distance;
      bestButton = entry.button;
    }
  }
  return bestButton;
}

QPushButton* focusDownFromEntry(const QVector<FocusNavEntry>& entries, const FocusNavEntry& current) {
  if (current.row == -1) {
    return current.col == 0 ? closestButtonInRow(entries, 0, 0)
                            : closestButtonInRow(entries, 0, 1);
  }
  if (current.row >= 0 && current.row < 2) {
    return closestButtonInRow(entries, current.row + 1, current.col);
  }
  if (current.row == 2) {
    const int targetCol = current.col == 0 ? 0 : 1;
    return closestButtonInRow(entries, 3, targetCol);
  }
  const int firstFollowUpRow = EventCatalog::buttonGridRowCount();
  if (current.row == 3) {
    QPushButton* followUpButton = closestButtonInRow(entries, firstFollowUpRow, 0);
    return followUpButton ? followUpButton : closestButtonInRow(entries, 0, 0);
  }
  if (current.row >= firstFollowUpRow) {
    QPushButton* below = closestButtonInRow(entries, current.row + 1, current.col);
    if (below) return below;
  }
  return closestButtonInRow(entries, 0, 0);
}

QPushButton* focusUpFromEntry(const QVector<FocusNavEntry>& entries, const FocusNavEntry& current,
                              QPushButton* homeTeamButton, QPushButton* awayTeamButton) {
  if (current.row == 0) {
    if (current.col == 0) {
      return homeTeamButton;
    }
    return awayTeamButton;
  }
  if (current.row >= 1 && current.row <= 2) {
    return closestButtonInRow(entries, current.row - 1, current.col);
  }
  if (current.row == 3) {
    return closestButtonInRow(entries, 2, current.col);
  }
  const int firstFollowUpRow = EventCatalog::buttonGridRowCount();
  if (current.row > firstFollowUpRow) {
    return closestButtonInRow(entries, current.row - 1, current.col);
  }
  if (current.col == 0) {
    return closestButtonInRow(entries, 3, 0);
  }
  return homeTeamButton;
}
} // namespace

GameControls::GameControls(QWidget* parent)
    : QWidget(parent),
      followUpState_(),
      gamePhase_(GamePhase::NotStarted),
      activeMainButton_(nullptr),
      teamSide_(TagSession::TeamSide::Unspecified),
      labelOverrides_(),
      arrangingFollowUps_(false),
      applyingTitleFit_(false),
      stackedHeightUpdateQueued_(false),
      breakUiToggle_(nullptr),
      breakFixtureActive_(false) {
  setMinimumWidth(kMinimumPanelWidthPx);
  buildUi();
  wireSignals();
  hideFollowUpButtons();
  resetGameTimeState();
  applyUiLanguage();
  installEventFilter(this);
  qApp->installEventFilter(this);
  for (QPushButton* button : {periodAdvanceButton_, homeTeamButton_, awayTeamButton_}) {
    if (button) button->installEventFilter(this);
  }
  for (QPushButton* button : mainEventButtons_) {
    if (button) button->installEventFilter(this);
  }
}

GameControls::~GameControls() {
  qApp->removeEventFilter(this);
}

void GameControls::setSessionTeamNames(const QString& homeName, const QString& awayName,
                                       const QString& homeColorHex, const QString& awayColorHex) {
  const QString homeTrimmed = homeName.trimmed();
  const QString awayTrimmed = awayName.trimmed();
  homeTeamFollowUpLabel_ = homeTrimmed.isEmpty() ? QStringLiteral("home") : homeTrimmed;
  awayTeamFollowUpLabel_ = awayTrimmed.isEmpty() ? QStringLiteral("away") : awayTrimmed;
  homeTeamColorHex_ = normalizeTeamColorHex(homeColorHex);
  awayTeamColorHex_ = normalizeTeamColorHex(awayColorHex);
  refreshTeamButtonLabels();
  updateTeamButtonSelectionVisual();
}

void GameControls::refreshTeamButtonLabels() {
  elideButtonLabel(homeTeamButton_, homeTeamFollowUpLabel_);
  elideButtonLabel(awayTeamButton_, awayTeamFollowUpLabel_);
}

void GameControls::setEventLabelOverrides(const EventLabelOverrides::Table& overrides) {
  if (labelOverrides_ == overrides) return;
  labelOverrides_ = overrides;
  applyUiLanguage();
  refreshOpenFollowUps();
}

void GameControls::setTagSession(TagSession* session) {
  if (labelOverrideConnection_) QObject::disconnect(labelOverrideConnection_);
  labelOverrideConnection_ = {};
  tagSession_ = session;
  if (!tagSession_) {
    setEventLabelOverrides(EventLabelOverrides::Table{});
    return;
  }
  labelOverrideConnection_ =
      connect(tagSession_, &TagSession::eventLabelOverridesChanged, this, [this]() {
        if (!tagSession_ || breakFixtureActive_) return;
        setEventLabelOverrides(tagSession_->eventLabelOverrides());
      });
  setEventLabelOverrides(tagSession_->eventLabelOverrides());
}

void GameControls::refreshMainButtonTitles() {
  if (applyingTitleFit_) return;
  applyingTitleFit_ = true;
  for (QLabel* titleLabel : mainButtonTitleLabels_) {
    if (!titleLabel) continue;
    const QString fullText = resolvedTitleText(titleLabel, labelOverrides_);
    titleLabel->setProperty("fullLabel", fullText);
    applyFittedTitle(titleLabel, Style::kButtonTitleMaxLines);
  }
  applyingTitleFit_ = false;
}

QString GameControls::selectedTeamLabel() const {
  if (teamSide_ == TagSession::TeamSide::Home) return homeTeamFollowUpLabel_;
  if (teamSide_ == TagSession::TeamSide::Away) return awayTeamFollowUpLabel_;
  return QString();
}

QColor GameControls::selectedTeamColor() const {
  const QString hex = teamSide_ == TagSession::TeamSide::Home   ? homeTeamColorHex_
                      : teamSide_ == TagSession::TeamSide::Away ? awayTeamColorHex_
                                                                : QString();
  if (hex.isEmpty()) return QColor();
  const QColor color(hex);
  return color.isValid() ? color : QColor();
}

void GameControls::notifyTagSequence(const QString& shortcutKey, const QString& stepLabel,
                                    bool chainOpen, bool startsChain,
                                    EventCatalog::EventId stepEventId) {
  emit tagSequenceAdvanced(shortcutKey, stepLabel, selectedTeamColor(), chainOpen, startsChain,
                           stepEventId);
}

const QList<QPushButton*>& GameControls::keyboardOrderButtons() const {
  return mainEventButtons_;
}

void GameControls::updateTeamButtonSelectionVisual() {
  applyTeamButtonSelectionStyle(homeTeamButton_, teamSide_ == TagSession::TeamSide::Home,
                                homeTeamColorHex_);
  applyTeamButtonSelectionStyle(awayTeamButton_, teamSide_ == TagSession::TeamSide::Away,
                                awayTeamColorHex_);
  refreshGameButtonMotion(homeTeamButton_, activeMainButton_);
  refreshGameButtonMotion(awayTeamButton_, activeMainButton_);
}

void GameControls::setTeamSide(TagSession::TeamSide side) {
  teamSide_ = side;
  updateTeamButtonSelectionVisual();
}

void GameControls::onHomeTeamButtonClicked() {
  setTeamSide(TagSession::TeamSide::Home);
}

void GameControls::onAwayTeamButtonClicked() {
  setTeamSide(TagSession::TeamSide::Away);
}

void GameControls::switchTeamSideToOppositeTeam() {
  if (teamSide_ == TagSession::TeamSide::Home) {
    onAwayTeamButtonClicked();
    if (awayTeamButton_) {
      awayTeamButton_->setFocus(Qt::TabFocusReason);
      flashButtonBorder(awayTeamButton_);
    }
  } else if (teamSide_ == TagSession::TeamSide::Away) {
    onHomeTeamButtonClicked();
    if (homeTeamButton_) {
      homeTeamButton_->setFocus(Qt::TabFocusReason);
      flashButtonBorder(homeTeamButton_);
    }
  }
}

void GameControls::applyUiLanguage() {
  applyControlChrome();
  for (QPushButton* followUpButton : followUpButtons_) {
    if (!followUpButton) continue;
    for (QLabel* titleLabel : followUpButton->findChildren<QLabel*>()) {
      if (titleLabel->property("gameEventName").toString().isEmpty()) continue;
      titleLabel->setTextFormat(Qt::PlainText);
      QStringList selections = followUpState_.selections();
      const QString actionKey = followUpButton->property("gameEventKey").toString();
      if (!actionKey.isEmpty()) selections.append(actionKey);
      const EventLabelOverrides::FollowUpPath path =
          followUpPathFor(followUpState_.mainEvent(), selections);
      const QString fullText = path.isEmpty() ? resolvedTitleText(titleLabel, labelOverrides_)
                                              : EventLabelResolver::labelForPath(path, labelOverrides_);
      titleLabel->setProperty("fullLabel", fullText);
    }
  }
  updateGameTimeUi(false);
  updateBreakToggleLabels();
  relayoutFollowUpButtons();
}

void GameControls::applyControlChrome() {
  mainEventButtonsByKey_.clear();
  for (QPushButton* button : mainEventButtons_) {
    if (!button) continue;
    const auto eventId =
        static_cast<EventCatalog::EventId>(button->property("gameEventId").toInt());
    const bool hidden = labelOverrides_.hidden(eventId);
    Style::setState(button, "controlsHidden", hidden);
    const Qt::Key key = EventControlsValidation::effectiveShortcut(eventId, labelOverrides_);
    const QString letter = EventControlsValidation::letterForKey(key);
    const QString caption = letter.isEmpty() ? EventCatalog::shortcutLabel(eventId) : letter;
    button->setProperty("shortcutHint", caption);
    for (QLabel* label : button->findChildren<QLabel*>(Qt::FindDirectChildrenOnly)) {
      label->setTextFormat(Qt::PlainText);
      if (label->property("gameEventName").toString().isEmpty()) label->setText(caption);
    }
    const QString fullLabel = EventLabelResolver::labelForId(eventId, labelOverrides_);
    button->setAccessibleName(
        hidden ? AppLocale::trUi("controls_editor.hidden_access").arg(fullLabel) : QString());
    if (!hidden && key != Qt::Key_unknown &&
        !mainEventButtonsByKey_.contains(static_cast<int>(key))) {
      mainEventButtonsByKey_.insert(static_cast<int>(key), button);
    }
  }
  refreshMainButtonTitles();
}

QStringList GameControls::visibleFollowUpChoices(const QStringList& actions) const {
  QStringList visible;
  visible.reserve(actions.size());
  for (const QString& action : actions) {
    QStringList path = followUpState_.selections();
    path.append(action);
    const EventCatalog::EventId actionId =
        eventIdAtEndOfPath(followUpState_.mainEvent(), path);
    const EventLabelOverrides::FollowUpPath followUpPath =
        followUpPathFor(followUpState_.mainEvent(), path);
    const bool hidden = followUpPath.isEmpty()
                            ? actionId != EventCatalog::EventId::None && labelOverrides_.hidden(actionId)
                            : labelOverrides_.hiddenOnPath(followUpPath);
    if (hidden) continue;
    visible.append(action);
  }
  return visible;
}

void GameControls::refreshOpenFollowUps() {
  if (followUpState_.isIdle()) return;
  const QStringList options =
      followUpState_.selections().isEmpty()
          ? FollowUpCatalog::firstLevelOptions(followUpState_.mainEvent())
          : FollowUpCatalog::optionsAfter(followUpState_.mainEvent(), followUpState_.selections());
  if (visibleFollowUpChoices(options).isEmpty()) {
    commitFollowUpFlow();
    return;
  }
  presentFollowUpChoices(options, followUpState_.stage());
}

bool GameControls::buttonIsSuppressed(const QPushButton* button) const {
  return button != nullptr && button->property("controlsHidden").toBool();
}

void GameControls::installControlContextMenu(QPushButton* button) {
  if (!button) return;
  button->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(button, &QPushButton::customContextMenuRequested, this,
          [this, button](const QPoint& position) {
            const auto eventId =
                static_cast<EventCatalog::EventId>(button->property("gameEventId").toInt());
            EventLabelOverrides::FollowUpPath path;
            const QString actionKey = button->property("gameEventKey").toString();
            if (!actionKey.isEmpty()) {
              QStringList selections = followUpState_.selections();
              selections.append(actionKey);
              path = followUpPathFor(followUpState_.mainEvent(), selections);
            }
            GameControlsContextMenu::popup(
                button, button->mapToGlobal(position), tagSession_, eventId,
                [this]() { emit editGameControlsRequested(); }, path);
          });
}

void GameControls::resetGameTimeState() {
  gamePhase_ = GamePhase::NotStarted;
  updateGameTimeUi(false);
}

void GameControls::restoreGamePhase(TagSession::QuarterPhase phase, int currentQuarterIndex) {
  switch (phase) {
    case TagSession::QuarterPhase::NotStarted:
      gamePhase_ = GamePhase::NotStarted;
      break;
    case TagSession::QuarterPhase::GameEnded:
      gamePhase_ = GamePhase::Ended;
      break;
    case TagSession::QuarterPhase::QuarterInProgress:
      switch (currentQuarterIndex) {
        case 0: gamePhase_ = GamePhase::Q1; break;
        case 1: gamePhase_ = GamePhase::Q2; break;
        case 2: gamePhase_ = GamePhase::Q3; break;
        case 3: gamePhase_ = GamePhase::Q4; break;
        default:
          qWarning("GameControls::restoreGamePhase: unexpected quarter index %d",
                   currentQuarterIndex);
          Q_ASSERT(currentQuarterIndex >= 0 && currentQuarterIndex < 4);
          gamePhase_ = GamePhase::NotStarted;
          break;
      }
      break;
  }
  updateGameTimeUi(false);
}

QString GameControls::currentPeriodName() const {
  switch (gamePhase_) {
    case GamePhase::Q1: return QStringLiteral("Q1");
    case GamePhase::Q2: return QStringLiteral("Q2");
    case GamePhase::Q3: return QStringLiteral("Q3");
    case GamePhase::Q4: return QStringLiteral("Q4");
    case GamePhase::NotStarted:
    case GamePhase::Ended:
      return QString();
  }
  return QString();
}

void GameControls::updateGameTimeUi(bool animateQuarterChange) {
  if (!periodAdvanceButton_ || !periodAdvanceTitleLabel_) return;

  const char* titleKey = "gamecontrols.start_game";
  switch (gamePhase_) {
    case GamePhase::NotStarted:
      titleKey = "gamecontrols.start_game";
      break;
    case GamePhase::Q1:
      titleKey = "gamecontrols.start_q2";
      break;
    case GamePhase::Q2:
      titleKey = "gamecontrols.start_q3";
      break;
    case GamePhase::Q3:
      titleKey = "gamecontrols.start_q4";
      break;
    case GamePhase::Q4:
    case GamePhase::Ended:
      titleKey = "gamecontrols.end_game";
      break;
  }
  periodAdvanceTitleLabel_->setProperty("fullLabel", AppLocale::trUi(titleKey));
  periodAdvanceButton_->setEnabled(gamePhase_ != GamePhase::Ended);
  periodAdvanceButton_->setMinimumWidth(0);
  refreshPeriodButtonTitle();
  updateQuarterTrack(animateQuarterChange);
}

void GameControls::updateQuarterTrack(bool animateQuarterChange) {
  const bool gameEnded = (gamePhase_ == GamePhase::Ended);
  int currentQuarterIndex = -1;
  switch (gamePhase_) {
    case GamePhase::Q1: currentQuarterIndex = 0; break;
    case GamePhase::Q2: currentQuarterIndex = 1; break;
    case GamePhase::Q3: currentQuarterIndex = 2; break;
    case GamePhase::Q4: currentQuarterIndex = 3; break;
    case GamePhase::NotStarted:
    case GamePhase::Ended:
      currentQuarterIndex = -1;
      break;
  }

  const int quarterCount = static_cast<int>(quarterSegments_.size());
  for (int quarterIndex = 0; quarterIndex < quarterCount; ++quarterIndex) {
    QString quarterState;
    if (gameEnded || (currentQuarterIndex >= 0 && quarterIndex < currentQuarterIndex)) {
      quarterState = QStringLiteral("complete");
    } else if (quarterIndex == currentQuarterIndex) {
      quarterState = QStringLiteral("current");
    } else {
      quarterState = QStringLiteral("empty");
    }

    auto* segment = qobject_cast<QuarterSegmentBar*>(quarterSegments_.at(quarterIndex));
    auto* label = qobject_cast<QuarterTrackLabel*>(quarterTrackLabels_.at(quarterIndex));
    if (segment) Style::setProp(segment, "quarterState", quarterState);
    if (label) Style::setProp(label, "quarterState", quarterState);

    const QString previousState = quarterSegmentStates_.at(quarterIndex);
    const bool becameCurrent = animateQuarterChange
        && quarterState == QLatin1String("current")
        && previousState != QLatin1String("current");

    QColor fillColor = Style::ThemeColors::quarterSegmentEmpty();
    qreal targetProgress = 0.0;
    if (quarterState == QLatin1String("complete")) {
      fillColor = Style::ThemeColors::quarterSegmentComplete();
      targetProgress = 1.0;
    } else if (quarterState == QLatin1String("current")) {
      fillColor = Style::ThemeColors::quarterSegmentCurrent();
      targetProgress = 1.0;
    }
    if (segment) segment->setFillColor(fillColor);

    const QColor labelColor = quarterState == QLatin1String("empty")
        ? Style::ThemeColors::faint()
        : Style::ThemeColors::foreground();
    const QEasingCurve curve = Motion::strongOut();
    if (becameCurrent && segment && label) {
      Motion::animateProperty(segment, "fillProgress", 0.0, 0, curve);
      Motion::animateProperty(segment, "fillProgress", targetProgress, Motion::kMilestone, curve);
      label->setTextColor(labelColor);
      Motion::pulseOnce(label, "textColor", labelColor, Style::ThemeColors::accent(), Motion::kEnter);
    } else {
      if (segment) Motion::animateProperty(segment, "fillProgress", targetProgress, 0, curve);
      if (label) Motion::animateColor(label, "textColor", labelColor, 0);
    }
    quarterSegmentStates_.at(quarterIndex) = quarterState;
  }

  if (!quarterTrack_) return;
  if (gamePhase_ == GamePhase::NotStarted) {
    quarterTrack_->setAccessibleName(QStringLiteral("Quarter progress, not started"));
  } else if (gameEnded) {
    quarterTrack_->setAccessibleName(QStringLiteral("Quarter progress, game ended"));
  } else {
    quarterTrack_->setAccessibleName(
        QStringLiteral("Quarter progress, %1 of %2")
            .arg(currentPeriodName())
            .arg(quarterCount));
  }
}

void GameControls::onPeriodAdvanceButtonClicked() {
  if (gamePhase_ == GamePhase::Ended) return;
  flashButtonBorder(periodAdvanceButton_);
  if (gamePhase_ == GamePhase::NotStarted) {
    gamePhase_ = GamePhase::Q1;
    updateGameTimeUi(true);
    emit gameStartRequested();
    return;
  }
  switch (gamePhase_) {
    case GamePhase::Q1: gamePhase_ = GamePhase::Q2; break;
    case GamePhase::Q2: gamePhase_ = GamePhase::Q3; break;
    case GamePhase::Q3: gamePhase_ = GamePhase::Q4; break;
    case GamePhase::Q4: gamePhase_ = GamePhase::Ended; break;
    default: return;
  }
  updateGameTimeUi(true);
  emit nextQuarterRequested();
}

void GameControls::setActiveMainButton(QPushButton* button) {
  if (activeMainButton_ == button) return;
  QPushButton* previous = activeMainButton_;
  if (activeMainButton_) Style::setState(activeMainButton_, "activeMain", false);
  activeMainButton_ = button;
  if (activeMainButton_) Style::setState(activeMainButton_, "activeMain", true);
  refreshGameButtonMotion(previous, activeMainButton_);
  refreshGameButtonMotion(activeMainButton_, activeMainButton_);
}

void GameControls::clearActiveMainButton() {
  if (!activeMainButton_) return;
  QPushButton* previous = activeMainButton_;
  Style::setState(previous, "activeMain", false);
  activeMainButton_ = nullptr;
  refreshGameButtonMotion(previous, nullptr);
}

void GameControls::bindMainEventButton(QPushButton* button, EventCatalog::EventId eventId) {
  const QString eventName = EventCatalog::canonicalName(eventId);
  const QString shortcutHint = EventCatalog::shortcutLabel(eventId);
  Q_ASSERT_X(!eventName.isEmpty(), "GameControls", "main event is missing a canonical name");
  button->setProperty("gameEventId", static_cast<int>(eventId));
  configureMainGameControlButton(button, eventName, shortcutHint);

  const Qt::Key shortcutKey = EventCatalog::shortcutKey(eventId);
  Q_ASSERT_X(shortcutKey != Qt::Key_unknown, "GameControls",
             "main event button is missing a keyboard key");
  const int key = static_cast<int>(shortcutKey);
  Q_ASSERT_X(!mainEventButtonsByKey_.contains(key), "GameControls",
             "two main events share a keyboard key");
  mainEventButtonsByKey_.insert(key, button);
  const int eventIdValue = static_cast<int>(eventId);
  Q_ASSERT_X(!mainEventButtonsById_.contains(eventIdValue), "GameControls",
             "two buttons share an event id");
  mainEventButtonsById_.insert(eventIdValue, button);
}

void GameControls::configureMainGameControlButton(QPushButton* button, const QString& eventName,
                                                  const QString& shortcutHint) {
  if (!button) return;
  button->setText(QString());
  button->setProperty("gameEventName", eventName);
  button->setProperty("shortcutHint", shortcutHint);
  auto* layout = new QVBoxLayout(button);
  layout->setContentsMargins(2, 4, 2, 4);
  layout->setSpacing(2);
  auto* titleLabel = new QLabel(eventName, button);
  titleLabel->setProperty("gameEventName", eventName);
  titleLabel->setProperty("gameEventId", button->property("gameEventId"));
  titleLabel->setTextFormat(Qt::PlainText);
  mainButtonTitleLabels_.append(titleLabel);
  titleLabel->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
  titleLabel->setWordWrap(true);
  titleLabel->setMinimumWidth(0);
  titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
  Style::setRole(titleLabel, "gameControlTitle");
  titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  auto* shortcutLabel = new QLabel(shortcutHint, button);
  shortcutLabel->setTextFormat(Qt::PlainText);
  shortcutLabel->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
  shortcutLabel->setMinimumWidth(0);
  shortcutLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  Style::setRole(shortcutLabel, "muted");
  shortcutLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  layout->addWidget(titleLabel);
  layout->addWidget(shortcutLabel);
}

void GameControls::configurePeriodAdvanceButton(QPushButton* button, const QString& shortcutHint) {
  if (!button) return;
  button->setText(QString());
  auto* layout = new QHBoxLayout(button);
  layout->setContentsMargins(4, 4, 4, 4);
  layout->setSpacing(8);
  periodAdvanceTitleLabel_ = new QLabel(button);
  periodAdvanceTitleLabel_->setTextFormat(Qt::PlainText);
  periodAdvanceTitleLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  periodAdvanceTitleLabel_->setWordWrap(false);
  periodAdvanceTitleLabel_->setMinimumWidth(0);
  periodAdvanceTitleLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  Style::setRole(periodAdvanceTitleLabel_, "gameControlTitle");
  periodAdvanceTitleLabel_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  auto* shortcutLabel = new QLabel(shortcutHint, button);
  shortcutLabel->setTextFormat(Qt::PlainText);
  shortcutLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  shortcutLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
  Style::setRole(shortcutLabel, "muted");
  shortcutLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  layout->addWidget(periodAdvanceTitleLabel_);
  layout->addWidget(shortcutLabel);
}

void GameControls::buildUi() {
  mainButtonTitleLabels_.clear();
  mainEventButtons_.clear();
  mainEventButtonsByKey_.clear();
  mainEventButtonsById_.clear();
  auto* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(12);

  // Game-time row: period-advance button | segmented quarter track
  auto* gameTimeRowWidget = new QWidget(this);
  auto* gameTimeRowLayout = new QHBoxLayout(gameTimeRowWidget);
  gameTimeRowLayout->setContentsMargins(0, 0, 0, 0);
  gameTimeRowLayout->setSpacing(8);

  periodAdvanceButton_ = new QPushButton(gameTimeRowWidget);
  Style::setVariant(periodAdvanceButton_, "gameControl");
  Style::setSize(periodAdvanceButton_, "sm");
  periodAdvanceButton_->setFocusPolicy(Qt::ClickFocus);
  periodAdvanceButton_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  periodAdvanceButton_->setMinimumWidth(0);
  configurePeriodAdvanceButton(periodAdvanceButton_, QStringLiteral("G"));

  quarterTrack_ = new QWidget(gameTimeRowWidget);
  Style::setRole(quarterTrack_, "quarterTrack");
  auto* trackLayout = new QVBoxLayout(quarterTrack_);
  trackLayout->setContentsMargins(0, 0, 0, 0);
  trackLayout->setSpacing(4);

  auto* segmentsRow = new QWidget(quarterTrack_);
  segmentsRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  auto* segmentsLayout = new QHBoxLayout(segmentsRow);
  segmentsLayout->setContentsMargins(0, 0, 0, 0);
  segmentsLayout->setSpacing(3);

  auto* labelsRow = new QWidget(quarterTrack_);
  auto* labelsLayout = new QHBoxLayout(labelsRow);
  labelsLayout->setContentsMargins(0, 0, 0, 0);
  labelsLayout->setSpacing(3);

  const std::array<QString, 4> quarterNames = {
      QStringLiteral("Q1"), QStringLiteral("Q2"), QStringLiteral("Q3"), QStringLiteral("Q4")};
  for (int quarterIndex = 0; quarterIndex < static_cast<int>(quarterNames.size()); ++quarterIndex) {
    auto* segment = new QuarterSegmentBar(segmentsRow);
    Style::setRole(segment, "quarterSegment");
    Style::setProp(segment, "quarterState", QStringLiteral("empty"));
    quarterSegments_.at(quarterIndex) = segment;
    segmentsLayout->addWidget(segment, 1);

    auto* label = new QuarterTrackLabel(quarterNames.at(quarterIndex), labelsRow);
    label->setAlignment(Qt::AlignCenter);
    Style::setRole(label, "quarterTrackLabel");
    Style::setProp(label, "quarterState", QStringLiteral("empty"));
    quarterTrackLabels_.at(quarterIndex) = label;
    labelsLayout->addWidget(label, 1);
  }

  trackLayout->addWidget(segmentsRow);
  trackLayout->addWidget(labelsRow);

  gameTimeRowLayout->addWidget(periodAdvanceButton_, 0);
  gameTimeRowLayout->addWidget(quarterTrack_, 1);

  auto* teamRowWidget = new QWidget(this);
  auto* teamRowLayout = new QHBoxLayout(teamRowWidget);
  teamRowLayout->setContentsMargins(0, 0, 0, 0);
  teamRowLayout->setSpacing(8);

  homeTeamButton_ = new QPushButton(teamRowWidget);
  awayTeamButton_ = new QPushButton(teamRowWidget);
  homeTeamButton_->setText(homeTeamFollowUpLabel_);
  awayTeamButton_->setText(awayTeamFollowUpLabel_);
  for (auto* btn : {homeTeamButton_, awayTeamButton_}) {
    Style::setSize(btn, "lg");
    Style::setVariant(btn, "gameControl");
    btn->setFocusPolicy(Qt::StrongFocus);
    // Ignored width: a long club name must not push the video column aside.
    btn->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    btn->setMinimumWidth(0);
    btn->setMinimumHeight(64);
  }
  teamRowLayout->addWidget(homeTeamButton_, 1);
  teamRowLayout->addWidget(awayTeamButton_, 1);
  setTabOrder(homeTeamButton_, awayTeamButton_);

  // QWERTY block. Row offsets come from the catalog so Q sits above the gap before A.
  auto* mainGridWidget = new QWidget(this);
  auto* keyboardRowsLayout = new QVBoxLayout(mainGridWidget);
  keyboardRowsLayout->setContentsMargins(0, 0, 0, 0);
  keyboardRowsLayout->setSpacing(4);

  const int keyboardRowCount = EventCatalog::buttonGridRowCount();
  int maximumLeftOffsetUnits = 0;
  for (int rowIndex = 0; rowIndex < keyboardRowCount; ++rowIndex) {
    maximumLeftOffsetUnits = std::max(
        maximumLeftOffsetUnits, EventCatalog::buttonGridRowAt(rowIndex).leftOffsetUnits);
  }

  for (int rowIndex = 0; rowIndex < keyboardRowCount; ++rowIndex) {
    const EventCatalog::ButtonGridRow gridRow = EventCatalog::buttonGridRowAt(rowIndex);
    QList<QPushButton*> rowButtons;
    rowButtons.reserve(gridRow.eventCount);
    for (int column = 0; column < gridRow.eventCount; ++column) {
      const EventCatalog::Node& node =
          EventCatalog::buttonGridNodeAt(gridRow.firstEventIndex + column);
      auto* button = new QPushButton(mainGridWidget);
      bindMainEventButton(button, node.id);
      Style::setSize(button, "md");
      Style::setVariant(button, "gameControl");
      // ClickFocus: not in Tab chain; main actions use letter shortcuts and mouse.
      button->setFocusPolicy(Qt::ClickFocus);
      button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
      button->setMinimumWidth(0);
      button->setMinimumHeight(52);
      rowButtons.append(button);
      mainEventButtons_.append(button);
    }
    addKeyboardRow(keyboardRowsLayout, mainGridWidget, rowButtons, gridRow.leftOffsetUnits,
                   maximumLeftOffsetUnits);
  }
  setFocusPolicy(Qt::StrongFocus);

  // Follow-up buttons container (initially hidden)
  followUpContainer_ = new QWidget(this);
  followUpContainer_->setObjectName(QStringLiteral("TaggingFollowUpRow"));
  followUpContainer_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
  followUpContainer_->setMinimumWidth(0);
  followUpLayout_ = new QGridLayout(followUpContainer_);
  followUpLayout_->setContentsMargins(0, 0, 0, 0);
  followUpLayout_->setSpacing(8);
  followUpContainer_->installEventFilter(this);

  mainLayout->addWidget(gameTimeRowWidget);
  mainLayout->addWidget(teamRowWidget);
  mainLayout->addWidget(mainGridWidget);
  mainLayout->addWidget(followUpContainer_);
  mainLayout->addStretch(1);

  QList<QPushButton*> motionButtons = {periodAdvanceButton_, homeTeamButton_, awayTeamButton_};
  motionButtons.append(mainEventButtons_);
  for (QPushButton* button : motionButtons) {
    refreshGameButtonMotion(button, activeMainButton_);
  }
  buildBreakToggle();
}

void GameControls::wireSignals() {
  if (periodAdvanceButton_) {
    connect(periodAdvanceButton_, &QPushButton::clicked, this,
            &GameControls::onPeriodAdvanceButtonClicked);
  }

  connect(homeTeamButton_, &QPushButton::clicked, this,
          [this]() { flashButtonBorder(homeTeamButton_); });
  connect(homeTeamButton_, &QPushButton::clicked, this, &GameControls::onHomeTeamButtonClicked);
  connect(awayTeamButton_, &QPushButton::clicked, this,
          [this]() { flashButtonBorder(awayTeamButton_); });
  connect(awayTeamButton_, &QPushButton::clicked, this, &GameControls::onAwayTeamButtonClicked);

  for (QPushButton* button : mainEventButtons_) {
    connect(button, &QPushButton::clicked, this, [this, button]() { flashButtonBorder(button); });
    connect(button, &QPushButton::clicked, this, &GameControls::onMainButtonClicked);
    installControlContextMenu(button);
  }
}

bool GameControls::handleApplicationShortcut(QKeyEvent* event) {
  if (!event || !isVisible() || event->modifiers() != Qt::NoModifier) {
    return false;
  }
  // Letter shortcuts tag the match. A focused text field must receive the key.
  if (TextInputFocus::widgetIsTextInput(QApplication::focusWidget())) {
    return false;
  }
  // Key repeat would close several quarters while G or H is held.
  const bool quarterAdvanceKey = event->key() == Qt::Key_G || event->key() == Qt::Key_H;
  if (event->isAutoRepeat() && quarterAdvanceKey) {
    return true;
  }

  auto clickIfReady = [](QPushButton* button) -> bool {
    if (!button || !button->isVisible() || !button->isEnabled()) {
      return false;
    }
    button->click();
    return true;
  };

  const auto mainEventButton = mainEventButtonsByKey_.constFind(event->key());
  if (mainEventButton != mainEventButtonsByKey_.cend()) {
    return clickIfReady(mainEventButton.value());
  }

  switch (event->key()) {
    case Qt::Key_G:
    case Qt::Key_H:
      return clickIfReady(periodAdvanceButton_);
    case Qt::Key_1:
    case Qt::Key_2:
    case Qt::Key_3:
    case Qt::Key_4:
    case Qt::Key_5:
    case Qt::Key_6:
    case Qt::Key_7:
    case Qt::Key_8:
    case Qt::Key_9: {
      if (!followUpContainer_ || !followUpContainer_->isVisible()) {
        return false;
      }
      const int followUpIndex = event->key() - Qt::Key_1;
      if (followUpIndex < 0 || followUpIndex >= followUpButtons_.size()) {
        return false;
      }
      return clickIfReady(followUpButtons_.at(followUpIndex));
    }
    case Qt::Key_Escape:
      if (followUpState_.isIdle()) {
        return false;
      }
      cancelFollowUpFlow();
      return true;
    default:
      return false;
  }
}

void GameControls::onMainButtonClicked() {
  auto* button = qobject_cast<QPushButton*>(sender());
  if (!button || buttonIsSuppressed(button)) return;

  if (teamSide_ == TagSession::TeamSide::Unspecified) {
    clearActiveMainButton();
    flashButtonBorder(homeTeamButton_);
    flashButtonBorder(awayTeamButton_);
    return;
  }

  QString eventName = button->property("gameEventName").toString();
  if (eventName.isEmpty()) {
    eventName = button->text();
  }
  const auto stepEventId =
      static_cast<EventCatalog::EventId>(button->property("gameEventId").toInt());
  followUpState_.beginMainEvent(eventName);
  setActiveMainButton(button);
  emit mainEventTimestampCaptured(eventName);
  const bool chainOpen =
      !visibleFollowUpChoices(FollowUpCatalog::firstLevelOptions(eventName)).isEmpty();
  notifyTagSequence(button->property("shortcutHint").toString(), eventName, chainOpen, true,
                    stepEventId);
  showFirstLevelFollowUps();
}

void GameControls::onFollowUpButtonClicked() {
  auto* button = qobject_cast<QPushButton*>(sender());
  if (!button) return;

  QString followUpName = button->property("gameEventKey").toString();
  if (followUpName.isEmpty()) followUpName = button->text();
  advanceFollowUpFlow(followUpName);
}

void GameControls::advanceFollowUpFlow(const QString& choice) {
  followUpState_.select(choice);
  const QStringList nextOptions =
      FollowUpCatalog::optionsAfter(followUpState_.mainEvent(), followUpState_.selections());
  const bool chainOpen = !visibleFollowUpChoices(nextOptions).isEmpty();
  const EventCatalog::EventId stepEventId =
      eventIdAtEndOfPath(followUpState_.mainEvent(), followUpState_.selections());
  notifyTagSequence(QString(), choice, chainOpen, false, stepEventId);
  if (!chainOpen) {
    commitFollowUpFlow();
    return;
  }
  presentFollowUpChoices(nextOptions, followUpState_.stageAfterSelection());
}

void GameControls::commitFollowUpFlow() {
  const QString teamLabel = selectedTeamLabel();
  const FollowUpState::Snapshot snapshot = followUpState_.takeCommit();
  const QString payload =
      FollowUpCatalog::formatPayload(snapshot.mainEvent, snapshot.selections, teamLabel);

  emit tagCommitted(snapshot.mainEvent, payload);

  if (FollowUpCatalog::switchesTeamOnCommit(snapshot.mainEvent) && !payload.isEmpty()) {
    switchTeamSideToOppositeTeam();
  }
  if (FollowUpCatalog::continuesAsGoal(snapshot.mainEvent, snapshot.selections)) {
    beginChainedGoalFlow();
    return;
  }
  clearFollowUpUi();
}

void GameControls::cancelFollowUpFlow() {
  if (followUpState_.isIdle()) return;
  const QString mainEvent = followUpState_.takeCommit().mainEvent;
  emit tagSequenceDismissed();
  emit tagCommitted(mainEvent, QString());
  clearFollowUpUi();
}

void GameControls::beginChainedGoalFlow() {
  // The scoring goal is already the last toast step. A second toast would hide
  // that chain. If that event grows follow-up questions, those still need their
  // own confirmation. Which chains start it is the catalog continuesAsGoal flag.
  const EventCatalog::Node* scoringGoal = EventCatalog::scoringGoalMainEvent();
  Q_ASSERT_X(scoringGoal, "GameControls", "catalog has no scoring goal");
  if (!scoringGoal) {
    clearFollowUpUi();
    return;
  }
  const QString eventName = EventCatalog::canonicalName(scoringGoal->id);
  QPushButton* goalButton =
      mainEventButtonsById_.value(static_cast<int>(scoringGoal->id), nullptr);
  Q_ASSERT_X(goalButton, "GameControls", "scoring goal is missing from the button grid");
  followUpState_.beginMainEvent(eventName);
  setActiveMainButton(goalButton);
  emit mainEventTimestampCaptured(eventName);
  const bool chainOpen =
      !visibleFollowUpChoices(FollowUpCatalog::firstLevelOptions(eventName)).isEmpty();
  if (chainOpen) {
    const QString shortcut = goalButton ? goalButton->property("shortcutHint").toString()
                                        : EventCatalog::shortcutLabel(scoringGoal->id);
    notifyTagSequence(shortcut, eventName, true, true, scoringGoal->id);
  }
  showFirstLevelFollowUps();
}

void GameControls::clearFollowUpUi() {
  clearActiveMainButton();
  hideFollowUpButtons();
}

void GameControls::showFirstLevelFollowUps() {
  const QStringList actions = FollowUpCatalog::firstLevelOptions(followUpState_.mainEvent());
  if (actions.isEmpty()) {
    commitFollowUpFlow();
    return;
  }
  presentFollowUpChoices(actions, FollowUpState::Stage::FirstLevel);
}

void GameControls::presentFollowUpChoices(const QStringList& actions, FollowUpState::Stage stage) {
  const QStringList visibleActions = visibleFollowUpChoices(actions);
  if (visibleActions.isEmpty()) {
    commitFollowUpFlow();
    return;
  }
  hideFollowUpButtons();

  for (int actionIndex = 0; actionIndex < visibleActions.size(); ++actionIndex) {
    const QString& action = visibleActions.at(actionIndex);
    QStringList path = followUpState_.selections();
    path.append(action);
    const EventCatalog::EventId actionId =
        eventIdAtEndOfPath(followUpState_.mainEvent(), path);
    const EventLabelOverrides::FollowUpPath followUpPath =
        followUpPathFor(followUpState_.mainEvent(), path);
    const QString displayLabel = followUpPath.isEmpty()
                                     ? (actionId == EventCatalog::EventId::None
                                            ? AppLocale::trEvent(action)
                                            : EventLabelResolver::labelForId(actionId, labelOverrides_))
                                     : EventLabelResolver::labelForPath(followUpPath, labelOverrides_);
    auto* button = new QPushButton(followUpContainer_);
    Style::setSize(button, "md");
    Style::setVariant(button, "gameControlFollowUp");
    configureFollowUpButton(button, action, actionId, displayLabel,
                            QString::number(actionIndex + 1));
    button->setFocusPolicy(Qt::ClickFocus);
    button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    button->setMinimumWidth(0);

    connect(button, &QPushButton::clicked, this, [this, button]() { flashButtonBorder(button); });
    connect(button, &QPushButton::clicked, this, &GameControls::onFollowUpButtonClicked);
    installControlContextMenu(button);
    refreshGameButtonMotion(button, activeMainButton_);
    button->installEventFilter(this);
    followUpButtons_.append(button);
  }

  followUpState_.setStage(stage);
  followUpContainer_->setVisible(true);
  if (QWidget* host = followUpContainer_->parentWidget()) {
    if (host->layout()) host->layout()->activate();
  }
  relayoutFollowUpButtons();
}

void GameControls::hideFollowUpButtons() {
  for (auto* button : followUpButtons_) {
    followUpLayout_->removeWidget(button);
    button->hide();
    button->deleteLater();
  }
  followUpButtons_.clear();
  while (QLayoutItem* item = followUpLayout_->takeAt(0)) {
    delete item;
  }
  followUpColumnCount_ = 1;
  followUpContainer_->setVisible(false);
}

void GameControls::flashButtonBorder(QPushButton* button) {
  if (!button) return;

  const Motion::ButtonColors colors = colorsForGameButton(button, activeMainButton_);
  QColor flashBackground = Style::ThemeColors::hoverSurface();
  QColor flashBorder = Style::ThemeColors::ring();
  const QString variant = button->property("variant").toString();
  if (variant == QLatin1String("gameControlFollowUp")) {
    flashBackground = QColor(QStringLiteral("#18181b"));
    flashBorder = flashBackground;
  } else if (button->property("teamSelected").toBool() || button == activeMainButton_) {
    flashBackground = Style::ThemeColors::pressedSurface();
    flashBorder = colors.restBorder;
  }
  Motion::flashButtonColors(button, flashBackground, flashBorder, kFlashDurationMs);
}

QList<QPushButton*> GameControls::focusableButtonsOrder() const {
  QList<QPushButton*> buttons;
  for (const FocusNavEntry& entry :
       buildFocusNavEntries(homeTeamButton_, awayTeamButton_, keyboardOrderButtons(), followUpButtons_,
                            followUpColumnCount_)) {
    if (entry.button) {
      buttons.append(entry.button);
    }
  }
  return buttons;
}

void GameControls::focusNextInDirection(Qt::Key key) {
  const QVector<FocusNavEntry> entries =
      buildFocusNavEntries(homeTeamButton_, awayTeamButton_, keyboardOrderButtons(), followUpButtons_,
                           followUpColumnCount_);
  if (entries.isEmpty()) {
    return;
  }

  QWidget* focus = focusWidget();
  int currentIndex = -1;
  for (int index = 0; index < entries.size(); ++index) {
    if (entries.at(index).button == focus) {
      currentIndex = index;
      break;
    }
  }
  if (currentIndex < 0) {
    if (entries.first().button) {
      entries.first().button->setFocus(Qt::OtherFocusReason);
    }
    return;
  }

  const FocusNavEntry& current = entries.at(currentIndex);
  QPushButton* nextButton = nullptr;
  if (key == Qt::Key_Right || key == Qt::Key_Left) {
    QVector<int> sameRowIndexes;
    for (int index = 0; index < entries.size(); ++index) {
      if (entries.at(index).row == current.row) {
        sameRowIndexes.append(index);
      }
    }
    std::sort(sameRowIndexes.begin(), sameRowIndexes.end(),
              [&](int leftIndex, int rightIndex) {
                return entries.at(leftIndex).col < entries.at(rightIndex).col;
              });
    const int positionInRow = sameRowIndexes.indexOf(currentIndex);
    if (positionInRow >= 0) {
      const int delta = key == Qt::Key_Right ? 1 : -1;
      const int nextPosition =
          (positionInRow + delta + sameRowIndexes.size()) % sameRowIndexes.size();
      nextButton = entries.at(sameRowIndexes.at(nextPosition)).button;
    }
  } else if (key == Qt::Key_Down) {
    nextButton = focusDownFromEntry(entries, current);
  } else if (key == Qt::Key_Up) {
    nextButton = focusUpFromEntry(entries, current, homeTeamButton_, awayTeamButton_);
  }

  if (nextButton) {
    nextButton->setFocus(Qt::TabFocusReason);
  }
}

void GameControls::applyTeamOnlyTabNavigation(bool forwardTab) {
  if (!homeTeamButton_ || !awayTeamButton_) return;
  QWidget* fw = focusWidget();
  QPushButton* target = nullptr;
  if (fw == homeTeamButton_) {
    target = awayTeamButton_;
  } else if (fw == awayTeamButton_) {
    target = homeTeamButton_;
  } else if (forwardTab) {
    target = homeTeamButton_;
  } else {
    target = awayTeamButton_;
  }
  if (!target) return;
  target->setFocus(Qt::TabFocusReason);
  flashButtonBorder(target);
  if (target == homeTeamButton_) {
    onHomeTeamButtonClicked();
  } else {
    onAwayTeamButtonClicked();
  }
}

bool GameControls::eventFilter(QObject* obj, QEvent* event) {
  if (!event) return QWidget::eventFilter(obj, event);

  const QEvent::Type eventType = event->type();
  const bool geometryEvent = eventType == QEvent::Resize || eventType == QEvent::Polish ||
                             eventType == QEvent::StyleChange || eventType == QEvent::FontChange;
  if (geometryEvent && !arrangingFollowUps_ && !applyingTitleFit_) {
    auto* button = qobject_cast<QPushButton*>(obj);
    const bool ownButton = button && (button == periodAdvanceButton_ || button == homeTeamButton_ ||
                                      button == awayTeamButton_ || mainEventButtons_.contains(button) ||
                                      followUpButtons_.contains(button));
    if (obj == this || obj == followUpContainer_ || ownButton) scheduleFittedRefresh();
  }
  if (event->type() == QEvent::KeyPress) {
    QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
    if (handleApplicationShortcut(keyEvent)) {
      return true;
    }
    const bool shiftTab = (keyEvent->key() == Qt::Key_Backtab) ||
                          (keyEvent->key() == Qt::Key_Tab && (keyEvent->modifiers() & Qt::ShiftModifier));
    const bool forwardTab =
        (keyEvent->key() == Qt::Key_Tab) && !(keyEvent->modifiers() & Qt::ShiftModifier);
    if (forwardTab || shiftTab) {
      QWidget* focus = QApplication::focusWidget();
      if (!isVisible() || !focus || (focus != this && !isAncestorOf(focus))) {
        return QWidget::eventFilter(obj, event);
      }
      applyTeamOnlyTabNavigation(forwardTab);
      return true;
    }
    QWidget* w = qobject_cast<QWidget*>(obj);
    if (w && (w == this || focusableButtonsOrder().contains(qobject_cast<QPushButton*>(w)))) {
      // Space is reserved for video play/pause (VideoControlsBar application shortcut), never for game controls.
      if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
        QWidget* focus = focusWidget();
        QPushButton* btn = qobject_cast<QPushButton*>(focus);
        if (btn && focusableButtonsOrder().contains(btn)) {
          flashButtonBorder(btn);
          btn->click();
          return true;
        }
      }
      if (keyEvent->key() == Qt::Key_Left || keyEvent->key() == Qt::Key_Right ||
          keyEvent->key() == Qt::Key_Up || keyEvent->key() == Qt::Key_Down) {
        focusNextInDirection(static_cast<Qt::Key>(keyEvent->key()));
        return true;
      }
    }
  }
  return QWidget::eventFilter(obj, event);
}

void GameControls::updateStackedButtonHeight(QPushButton* button) {
  if (!button) return;
  QLabel* titleLabel = titleLabelOnButton(button);
  if (!titleLabel) return;
  QLabel* shortcutLabel = shortcutLabelOnButton(button);

  const bool compact =
      titleLabel->property("titleDensity").toString() == QLatin1String("compact");
  titleLabel->ensurePolished();
  const QFontMetrics titleMetrics(themeTitleFont(titleLabel));
  const QString accentSample = QStringLiteral("Áy\nÁy");
  const int wrappedSampleHeight = titleMetrics
                                       .boundingRect(QRect(0, 0, 400, 400),
                                                     Qt::AlignHCenter | Qt::TextWordWrap, accentSample)
                                       .height();
  const int twoLineHeight =
      qMax(titleMetrics.lineSpacing() * Style::kButtonTitleMaxLines, wrappedSampleHeight);
  if (compact) {
    if (titleLabel->minimumHeight() < twoLineHeight) titleLabel->setMinimumHeight(twoLineHeight);
  } else if (titleLabel->minimumHeight() != twoLineHeight) {
    titleLabel->setMinimumHeight(twoLineHeight);
  }

  if (shortcutLabel) {
    shortcutLabel->ensurePolished();
    const int shortcutHeight =
        qMax(shortcutLabel->fontMetrics().lineSpacing(), shortcutLabel->fontMetrics().height());
    if (shortcutLabel->minimumHeight() != shortcutHeight) {
      shortcutLabel->setMinimumHeight(shortcutHeight);
    }
    shortcutLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  }

  if (QLayout* layout = button->layout()) layout->activate();
  const int contentHeight =
      button->layout() ? button->layout()->minimumSize().height() : twoLineHeight;
  button->ensurePolished();
  QStyleOptionButton option;
  option.initFrom(button);
  const int styledHeight =
      button->style()
          ->sizeFromContents(QStyle::CT_PushButton, &option, QSize(0, contentHeight), button)
          .height();
  const int minimumHeight = qMax(contentHeight, styledHeight);
  if (button->minimumHeight() != minimumHeight) button->setMinimumHeight(minimumHeight);
}

void GameControls::updateAllStackedButtonHeights() {
  for (QPushButton* button : mainEventButtons_) updateStackedButtonHeight(button);
  for (QPushButton* button : followUpButtons_) updateStackedButtonHeight(button);
}

void GameControls::refreshFollowUpTitles() {
  if (applyingTitleFit_) return;
  applyingTitleFit_ = true;
  for (QPushButton* button : followUpButtons_) {
    applyFittedTitle(titleLabelOnButton(button), Style::kButtonTitleMaxLines);
  }
  applyingTitleFit_ = false;
}

void GameControls::refreshPeriodButtonTitle() {
  if (!periodAdvanceTitleLabel_ || applyingTitleFit_) return;
  applyingTitleFit_ = true;
  applyFittedTitle(periodAdvanceTitleLabel_, 1);
  applyingTitleFit_ = false;
}

int GameControls::columnCountForFollowUps(int availableWidth) const {
  QStringList labels;
  const QLabel* sampleTitle = nullptr;
  for (const QPushButton* button : followUpButtons_) {
    const QLabel* titleLabel = titleLabelOnButton(button);
    if (!titleLabel) continue;
    labels.append(titleLabel->property("fullLabel").toString());
    if (!sampleTitle) sampleTitle = titleLabel;
  }
  if (labels.size() <= 1) return 1;

  auto* mutableTitle = const_cast<QLabel*>(sampleTitle);
  mutableTitle->ensurePolished();
  const QFontMetrics metrics(themeTitleFont(sampleTitle));

  int horizontalChrome = -1;
  for (const QPushButton* button : mainEventButtons_) {
    horizontalChrome = measuredTitleChrome(button);
    if (horizontalChrome >= 0) break;
  }
  if (horizontalChrome < 0) {
    for (const QPushButton* button : followUpButtons_) {
      horizontalChrome = measuredTitleChrome(button);
      if (horizontalChrome >= 0) break;
    }
  }
  if (horizontalChrome < 0) {
    const QPushButton* sampleButton =
        mainEventButtons_.isEmpty() ? followUpButtons_.value(0, nullptr) : mainEventButtons_.first();
    if (sampleButton) {
      auto* mutableButton = const_cast<QPushButton*>(sampleButton);
      mutableButton->ensurePolished();
      QStyleOptionButton option;
      option.initFrom(mutableButton);
      horizontalChrome = mutableButton->style()
                             ->sizeFromContents(QStyle::CT_PushButton, &option, QSize(0, 0),
                                                mutableButton)
                             .width();
    } else {
      horizontalChrome = 0;
    }
  }

  const int spacing = followUpLayout_ ? followUpLayout_->spacing() : 0;
  return Style::fittingColumnCount(availableWidth, labels.size(), horizontalChrome, spacing, metrics,
                                   labels);
}

void GameControls::relayoutFollowUpButtons() {
  if (arrangingFollowUps_ || !followUpLayout_ || !followUpContainer_) return;
  arrangingFollowUps_ = true;

  const int buttonCount = followUpButtons_.size();
  if (buttonCount == 0) {
    followUpColumnCount_ = 1;
    arrangingFollowUps_ = false;
    return;
  }

  int availableWidth = followUpContainer_->width();
  if (availableWidth <= 0) availableWidth = width();
  if (availableWidth <= 0) availableWidth = kMinimumPanelWidthPx;

  const int columnCount = columnCountForFollowUps(availableWidth);
  const bool gridMatches =
      followUpLayout_->count() == buttonCount && followUpColumnCount_ == columnCount;
  if (gridMatches) {
    arrangingFollowUps_ = false;
    refreshFollowUpTitles();
    return;
  }

  while (QLayoutItem* item = followUpLayout_->takeAt(0)) {
    delete item;
  }
  const int stretchColumns = qMax(followUpColumnCount_, columnCount) + 2;
  for (int column = 0; column < stretchColumns; ++column) {
    followUpLayout_->setColumnStretch(column, 0);
  }

  followUpColumnCount_ = columnCount;
  for (int index = 0; index < buttonCount; ++index) {
    QPushButton* button = followUpButtons_.at(index);
    if (!button) continue;
    followUpLayout_->addWidget(button, index / columnCount, index % columnCount);
    updateStackedButtonHeight(button);
  }
  for (int column = 0; column < columnCount; ++column) {
    followUpLayout_->setColumnStretch(column, 1);
  }
  followUpLayout_->activate();
  if (layout()) layout()->activate();
  arrangingFollowUps_ = false;
  refreshFollowUpTitles();
}

void GameControls::clearFollowUpPreview() {
  followUpState_.reset();
  clearActiveMainButton();
  hideFollowUpButtons();
}

void GameControls::previewMainEventFollowUps(const QString& mainEvent) {
  clearFollowUpPreview();
  QPushButton* mainButton = nullptr;
  for (QPushButton* button : mainEventButtons_) {
    if (button && button->property("gameEventName").toString() == mainEvent) {
      mainButton = button;
      break;
    }
  }
  if (!mainButton) return;

  followUpState_.beginMainEvent(mainEvent);
  setActiveMainButton(mainButton);
  const QStringList options = FollowUpCatalog::firstLevelOptions(mainEvent);
  if (visibleFollowUpChoices(options).isEmpty()) {
    clearFollowUpPreview();
    return;
  }
  presentFollowUpChoices(options, FollowUpState::Stage::FirstLevel);
}

void GameControls::buildBreakToggle() {
  if (breakUiToggle_ || !qEnvironmentVariableIsSet("AVA_BREAK_UI")) return;

  auto toggle = makeQtPtr<QWidget>(this);
  breakUiToggle_ = toggle.get();
  breakUiToggle_->setObjectName(QStringLiteral("TaggingBreakUiToggle"));
  Style::setRole(breakUiToggle_, "breakUiToggle");
  breakUiToggle_->setFocusPolicy(Qt::NoFocus);

  auto grid = makeQtPtr<QGridLayout>(breakUiToggle_);
  grid->setContentsMargins(4, 4, 4, 4);
  grid->setHorizontalSpacing(4);
  grid->setVerticalSpacing(4);

  struct BreakSegment {
    const char* objectName;
    int fixtureId;
    int row;
    int column;
  };
  const std::array<BreakSegment, 5> segments = {{
      {"TaggingBreakUiDemo", 0, 0, 0},
      {"TaggingBreakUiWorst", 1, 0, 1},
      {"TaggingBreakUiEmpty", 2, 0, 2},
      {"TaggingBreakUiOne", 3, 1, 0},
      {"TaggingBreakUiMany", 4, 1, 1},
  }};
  for (const BreakSegment& segment : segments) {
    auto button = makeQtPtr<QPushButton>(breakUiToggle_);
    button->setObjectName(QString::fromUtf8(segment.objectName));
    button->setProperty("breakFixture", segment.fixtureId);
    button->setCheckable(true);
    button->setChecked(segment.fixtureId == 0);
    button->setFocusPolicy(Qt::NoFocus);
    button->setMinimumWidth(0);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    Style::setRole(button.get(), "breakUiSegment");
    grid->addWidget(button.get(), segment.row, segment.column);
    connect(button.get(), &QPushButton::clicked, this, [this, fixtureId = segment.fixtureId]() {
      applyBreakFixture(fixtureId);
    });
  }
  updateBreakToggleLabels();
  positionBreakToggle();
}

void GameControls::updateBreakToggleLabels() {
  if (!breakUiToggle_) return;
  const QHash<QString, const char*> labelKeys = {
      {QStringLiteral("TaggingBreakUiDemo"), "controls_editor.break_demo"},
      {QStringLiteral("TaggingBreakUiWorst"), "controls_editor.break_worst"},
      {QStringLiteral("TaggingBreakUiEmpty"), "controls_editor.break_empty"},
      {QStringLiteral("TaggingBreakUiOne"), "controls_editor.break_one"},
      {QStringLiteral("TaggingBreakUiMany"), "controls_editor.break_many"},
  };
  for (QPushButton* button : breakUiToggle_->findChildren<QPushButton*>()) {
    const auto key = labelKeys.constFind(button->objectName());
    if (key == labelKeys.cend()) continue;
    button->setText(AppLocale::trUi(key.value()));
  }
}

void GameControls::positionBreakToggle() {
  if (!breakUiToggle_) return;
  breakUiToggle_->ensurePolished();
  const int horizontalInset = 8;
  const int bottomInset = 8;
  const int toggleWidth = qMax(0, width() - horizontalInset * 2);
  breakUiToggle_->setFixedWidth(toggleWidth);
  const int toggleHeight = qMax(breakUiToggle_->sizeHint().height(), breakUiToggle_->minimumSizeHint().height());
  const int top = qMax(0, height() - toggleHeight - bottomInset);
  breakUiToggle_->setGeometry(horizontalInset, top, toggleWidth, toggleHeight);
  breakUiToggle_->raise();
  breakUiToggle_->show();

  if (QLayout* panelLayout = layout()) {
    const int reserved = toggleHeight + bottomInset;
    QMargins margins = panelLayout->contentsMargins();
    if (margins.bottom() != reserved) {
      margins.setBottom(reserved);
      panelLayout->setContentsMargins(margins);
    }
  }
}

void GameControls::applyBreakFixture(int fixtureId) {
  constexpr int kDemoFixture = 0;
  constexpr int kWorstFixture = 1;
  constexpr int kEmptyFixture = 2;
  constexpr int kOneFixture = 3;
  constexpr int kManyFixture = 4;

  auto selectSegment = [this](int selectedFixture) {
    if (!breakUiToggle_) return;
    for (QPushButton* button : breakUiToggle_->findChildren<QPushButton*>()) {
      button->setChecked(button->property("breakFixture").toInt() == selectedFixture);
    }
  };

  if (fixtureId == kDemoFixture && !breakFixtureActive_) {
    selectSegment(kDemoFixture);
    return;
  }

  if (!breakFixtureActive_) {
    breakSavedOverrides_ = labelOverrides_;
    breakSavedHomeName_ = homeTeamFollowUpLabel_;
    breakSavedAwayName_ = awayTeamFollowUpLabel_;
    breakSavedHomeColor_ = homeTeamColorHex_;
    breakSavedAwayColor_ = awayTeamColorHex_;
  }
  clearFollowUpPreview();

  if (fixtureId == kDemoFixture) {
    breakFixtureActive_ = false;
    if (tagSession_) setEventLabelOverrides(tagSession_->eventLabelOverrides());
    else setEventLabelOverrides(breakSavedOverrides_);
    setSessionTeamNames(breakSavedHomeName_, breakSavedAwayName_, breakSavedHomeColor_,
                        breakSavedAwayColor_);
    selectSegment(kDemoFixture);
    return;
  }

  breakFixtureActive_ = true;
  EventLabelOverrides::Table table;
  auto commitLabel = [&table](EventCatalog::EventId eventId, const char* text) {
    EventControlsValidation::Issue issue;
    EventControlsValidation::commitLabel(&table, eventId, QString::fromUtf8(text), &issue);
  };
  auto hideEvent = [&table](EventCatalog::EventId eventId) {
    EventControlsValidation::commitShown(&table, eventId, false, nullptr);
  };

  if (fixtureId == kWorstFixture) {
    commitLabel(EventCatalog::EventId::Shot, "Aleksandra Wiśniewska-Kowalczyk");
    commitLabel(EventCatalog::EventId::Goal, "J");
    commitLabel(EventCatalog::EventId::Pass, "Jo");
    commitLabel(EventCatalog::EventId::CircleEntry, "Đặng Thị Ngọc Hân");
    commitLabel(EventCatalog::EventId::Card, "Benachrichtigungseinstellungen");
    commitLabel(EventCatalog::EventId::PenaltyCorner, "customer-feedback-from-enterprise-onboarding");
    commitLabel(EventCatalog::EventId::Turnover, "🦊 Fox");
    commitLabel(EventCatalog::EventId::Special, "<script>alert(1)</script>");
    commitLabel(EventCatalog::EventId::PenaltyCornerFoul, "María José de la Cruz y Fernández");
    commitLabel(EventCatalog::EventId::FiftyYard, "王秀英");
    commitLabel(EventCatalog::EventId::SeventyFiveYard,
                "Senior Product Design Engineer, Platform Infrastructure");
    commitLabel(EventCatalog::EventId::PenaltyStroke, "Ólafur Darri");
    commitLabel(EventCatalog::EventId::Shootout, "Seán O'Brien");
    commitLabel(EventCatalog::EventId::Interception, "Intercepción");
    commitLabel(EventCatalog::EventId::Tackle, "J");
    commitLabel(EventCatalog::EventId::Pressure, "Presión");
    commitLabel(EventCatalog::EventId::UnforcedError, "Konstantin Oberhauser-Wettstein");
    setEventLabelOverrides(table);
    setSessionTeamNames(QStringLiteral("Aleksandra Wiśniewska-Kowalczyk"),
                        QStringLiteral("Đặng Thị Ngọc Hân"), QStringLiteral("#fde68a"),
                        QStringLiteral("#1d4ed8"));
    previewMainEventFollowUps(QStringLiteral("Turnover"));
  } else if (fixtureId == kEmptyFixture) {
    setEventLabelOverrides(table);
  } else if (fixtureId == kOneFixture) {
    hideEvent(EventCatalog::EventId::Tackle);
    hideEvent(EventCatalog::EventId::Pressure);
    hideEvent(EventCatalog::EventId::UnforcedError);
    commitLabel(EventCatalog::EventId::Interception, "Intercepción");
    setEventLabelOverrides(table);
    previewMainEventFollowUps(QStringLiteral("Turnover"));
  } else if (fixtureId == kManyFixture) {
    commitLabel(EventCatalog::EventId::Interception, "Intercepción de contraataque");
    commitLabel(EventCatalog::EventId::Tackle, "Entrada por la izquierda");
    commitLabel(EventCatalog::EventId::Pressure, "Presión alta sostenida");
    commitLabel(EventCatalog::EventId::UnforcedError, "Error no forzado en salida");
    setEventLabelOverrides(table);
    previewMainEventFollowUps(QStringLiteral("Turnover"));
  }

  selectSegment(fixtureId);
}

void GameControls::scheduleFittedRefresh() {
  if (stackedHeightUpdateQueued_ || applyingTitleFit_ || arrangingFollowUps_) return;
  stackedHeightUpdateQueued_ = true;
  QTimer::singleShot(0, this, [guard = QPointer<GameControls>(this)]() {
    if (!guard) return;
    guard->stackedHeightUpdateQueued_ = false;
    guard->refreshFittedControls();
  });
}

void GameControls::refreshFittedControls() {
  if (QLayout* panelLayout = layout()) panelLayout->activate();
  updateAllStackedButtonHeights();
  if (QLayout* panelLayout = layout()) panelLayout->activate();
  refreshTeamButtonLabels();
  relayoutFollowUpButtons();
  if (QLayout* panelLayout = layout()) panelLayout->activate();
  refreshMainButtonTitles();
  refreshFollowUpTitles();
  refreshPeriodButtonTitle();
  positionBreakToggle();
}

void GameControls::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  scheduleFittedRefresh();
}

void GameControls::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  scheduleFittedRefresh();
}

void GameControls::keyPressEvent(QKeyEvent* event) {
  const bool shiftTab =
      (event->key() == Qt::Key_Backtab) ||
      (event->key() == Qt::Key_Tab && (event->modifiers() & Qt::ShiftModifier));
  const bool forwardTab = (event->key() == Qt::Key_Tab) && !(event->modifiers() & Qt::ShiftModifier);
  if (forwardTab || shiftTab) {
    applyTeamOnlyTabNavigation(forwardTab);
    event->accept();
    return;
  }
  // Space is reserved for video play/pause (VideoControlsBar application shortcut), never for game controls.
  if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
    QWidget* focus = focusWidget();
    QPushButton* btn = qobject_cast<QPushButton*>(focus);
    if (btn && focusableButtonsOrder().contains(btn)) {
      flashButtonBorder(btn);
      btn->click();
      event->accept();
      return;
    }
  }
  if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right ||
      event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) {
    QList<QPushButton*> list = focusableButtonsOrder();
    if (!list.isEmpty() && !list.contains(qobject_cast<QPushButton*>(focusWidget())))
      list.first()->setFocus(Qt::OtherFocusReason);
    else
      focusNextInDirection(static_cast<Qt::Key>(event->key()));
    event->accept();
    return;
  }
  QWidget::keyPressEvent(event);
}

#include "GameControls.moc"
