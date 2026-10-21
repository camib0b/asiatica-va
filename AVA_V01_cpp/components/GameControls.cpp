#include "GameControls.h"
#include "FollowUpCatalog.h"
#include "../style/Motion.h"
#include "../style/StyleProps.h"
#include "../style/ThemeColors.h"
#include "../i18n/AppLocale.h"
#include "../state/TagSession.h"
#include "../ui/TextInputFocus.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSpacerItem>
#include <QPushButton>
#include <QWidget>
#include <QTimer>
#include <QPointer>
#include <QKeyEvent>
#include <algorithm>
#include <array>
#include <climits>
#include <QEvent>
#include <QApplication>
#include <QLabel>
#include <QFontMetrics>
#include <QColor>
#include <QSizePolicy>
#include <QLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>

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

void configureFollowUpButton(QPushButton* button, const QString& canonicalKey,
                             const QString& shortcutHint) {
  if (!button) return;
  button->setText(QString());
  button->setProperty("gameEventKey", canonicalKey);
  auto* layout = new QVBoxLayout(button);
  layout->setContentsMargins(2, 4, 2, 4);
  layout->setSpacing(2);
  auto* titleLabel = new QLabel(AppLocale::trEvent(canonicalKey), button);
  titleLabel->setProperty("gameEventName", canonicalKey);
  titleLabel->setAlignment(Qt::AlignCenter);
  titleLabel->setWordWrap(true);
  Style::setRole(titleLabel, "gameControlTitle");
  titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  auto* shortcutLabel = new QLabel(shortcutHint, button);
  shortcutLabel->setAlignment(Qt::AlignCenter);
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

// One key is four layout units so a row can shift by quarter-keys (1 and 3).
constexpr int kKeyWidthUnits = 4;
constexpr int kHomeRowOffsetUnits = 1;    // ~1/4 key, matching the A row under Q
constexpr int kBottomRowOffsetUnits = 3;  // ~3/4 key, matching the Z row under Q
constexpr int kFullRowButtonCount = 4;
constexpr int kFollowUpFocusRow = 4;

void addKeyboardRow(QVBoxLayout* column, QWidget* parent, const QList<QPushButton*>& buttons,
                    int leftOffsetUnits) {
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
    layout->addWidget(button, kKeyWidthUnits);
  }

  const int buttonCount = static_cast<int>(buttons.size());
  const int missingButtons = qMax(0, kFullRowButtonCount - buttonCount);
  const int rightOffsetUnits =
      qMax(0, kBottomRowOffsetUnits - leftOffsetUnits) + (kKeyWidthUnits * missingButtons);
  auto* rightSpacer = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Minimum);
  layout->addSpacerItem(rightSpacer);
  layout->setStretch(layout->count() - 1, rightOffsetUnits);

  column->addWidget(row);
}

void elideButtonLabel(QPushButton* button, const QString& fullText) {
  if (!button) return;
  const int availableWidth = button->contentsRect().width();
  QString shown = fullText;
  if (availableWidth > 8) {
    shown = button->fontMetrics().elidedText(fullText, Qt::ElideRight, availableWidth);
  }
  if (button->text() != shown) {
    button->setText(shown);
  }
  button->setToolTip(shown == fullText ? QString() : fullText);
}

QVector<FocusNavEntry> buildFocusNavEntries(QPushButton* homeTeamButton,
                                            QPushButton* awayTeamButton,
                                            const std::array<QPushButton*, 14>& mainButtons,
                                            const QList<QPushButton*>& followUpButtons) {
  QVector<FocusNavEntry> entries;
  if (homeTeamButton) {
    entries.append({homeTeamButton, -1, 0});
  }
  if (awayTeamButton) {
    entries.append({awayTeamButton, -1, 1});
  }
  for (int index = 0; index < static_cast<int>(mainButtons.size()); ++index) {
    QPushButton* button = mainButtons.at(static_cast<std::size_t>(index));
    if (!button) continue;
    const int row = index / kFullRowButtonCount;
    const int column = index % kFullRowButtonCount;
    entries.append({button, row, column});
  }
  for (int index = 0; index < followUpButtons.size(); ++index) {
    QPushButton* button = followUpButtons.at(index);
    if (button && button->isVisible()) {
      entries.append({button, kFollowUpFocusRow, index});
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
  if (current.row == 3) {
    const int followUpRow = current.row + 1;
    QPushButton* followUpButton = closestButtonInRow(entries, followUpRow, 0);
    return followUpButton ? followUpButton : closestButtonInRow(entries, 0, 0);
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
      teamSide_(TagSession::TeamSide::Unspecified) {
  setMinimumWidth(kMinimumPanelWidthPx);
  buildUi();
  wireSignals();
  hideFollowUpButtons();
  resetGameTimeState();
  applyUiLanguage();
  installEventFilter(this);
  qApp->installEventFilter(this);
  for (auto* btn : {periodAdvanceButton_, homeTeamButton_, awayTeamButton_,
                    sixteenYardButton_, fiftyYardButton_, seventyFiveYardButton_,
                    pcButton_, circleEntryButton_, pcFoulButton_, shotButton_, goalButton_, passButton_,
                    specialButton_, turnoverButton_, cardButton_, shootoutButton_, psButton_})
    if (btn) btn->installEventFilter(this);
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

void GameControls::refreshMainButtonTitles() {
  for (QLabel* titleLabel : mainButtonTitleLabels_) {
    if (!titleLabel) continue;
    const QString fullText = AppLocale::trEvent(titleLabel->property("gameEventName").toString());
    const int availableWidth = titleLabel->contentsRect().width();
    const int availableHeight = titleLabel->contentsRect().height();
    const QFontMetrics metrics = titleLabel->fontMetrics();
    QString shown = fullText;
    if (availableWidth > 8 && availableHeight > 0) {
      const QRect wrapped = metrics.boundingRect(QRect(0, 0, availableWidth, 1000),
                                                 Qt::AlignHCenter | Qt::TextWordWrap, fullText);
      const bool fitsWrapped =
          wrapped.width() <= availableWidth + 1 && wrapped.height() <= availableHeight + 1;
      if (!fitsWrapped) {
        shown = metrics.elidedText(fullText, Qt::ElideRight, availableWidth);
      }
    }
    if (titleLabel->text() != shown) titleLabel->setText(shown);
    const QString tip = shown == fullText ? QString() : fullText;
    if (titleLabel->toolTip() != tip) titleLabel->setToolTip(tip);
    if (QPushButton* button = qobject_cast<QPushButton*>(titleLabel->parentWidget())) {
      if (button->toolTip() != tip) button->setToolTip(tip);
    }
  }
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
                                    bool chainOpen, bool startsChain) {
  emit tagSequenceAdvanced(shortcutKey, stepLabel, selectedTeamColor(), chainOpen, startsChain);
}

std::array<QPushButton*, 14> GameControls::keyboardOrderButtons() const {
  return {sixteenYardButton_, fiftyYardButton_, seventyFiveYardButton_, pcButton_,
          circleEntryButton_, pcFoulButton_, shotButton_, goalButton_,
          passButton_, specialButton_, turnoverButton_, cardButton_,
          psButton_, shootoutButton_};
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
  for (QLabel* titleLabel : mainButtonTitleLabels_) {
    if (!titleLabel) continue;
    const QString key = titleLabel->property("gameEventName").toString();
    titleLabel->setText(AppLocale::trEvent(key));
  }
  refreshMainButtonTitles();
  for (QPushButton* followUpButton : followUpButtons_) {
    if (!followUpButton) continue;
    for (QLabel* titleLabel : followUpButton->findChildren<QLabel*>()) {
      const QString key = titleLabel->property("gameEventName").toString();
      if (key.isEmpty()) continue;
      titleLabel->setText(AppLocale::trEvent(key));
    }
  }
  updateGameTimeUi(false);
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
  periodAdvanceTitleLabel_->setText(AppLocale::trUi(titleKey));
  periodAdvanceButton_->setEnabled(gamePhase_ != GamePhase::Ended);
  if (QLayout* buttonLayout = periodAdvanceButton_->layout()) {
    periodAdvanceTitleLabel_->ensurePolished();
    periodAdvanceButton_->setMinimumWidth(buttonLayout->sizeHint().width());
  }
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
  mainButtonTitleLabels_.append(titleLabel);
  titleLabel->setAlignment(Qt::AlignCenter);
  titleLabel->setWordWrap(true);
  titleLabel->setMinimumWidth(0);
  titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  Style::setRole(titleLabel, "gameControlTitle");
  titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  auto* shortcutLabel = new QLabel(shortcutHint, button);
  shortcutLabel->setAlignment(Qt::AlignCenter);
  shortcutLabel->setMinimumWidth(0);
  shortcutLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
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
  periodAdvanceTitleLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  Style::setRole(periodAdvanceTitleLabel_, "gameControlTitle");
  periodAdvanceTitleLabel_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  auto* shortcutLabel = new QLabel(shortcutHint, button);
  shortcutLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  Style::setRole(shortcutLabel, "muted");
  shortcutLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  layout->addWidget(periodAdvanceTitleLabel_);
  layout->addWidget(shortcutLabel);
}

void GameControls::buildUi() {
  mainButtonTitleLabels_.clear();
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

  // QWERTY block. Rows shift by a fraction of a key so Q sits above the gap before A.
  auto* mainGridWidget = new QWidget(this);
  auto* keyboardRowsLayout = new QVBoxLayout(mainGridWidget);
  keyboardRowsLayout->setContentsMargins(0, 0, 0, 0);
  keyboardRowsLayout->setSpacing(4);

  // Create main event buttons (title + keyboard hint on second line)
  sixteenYardButton_ = new QPushButton(mainGridWidget);
  configureMainGameControlButton(sixteenYardButton_, QStringLiteral("16-yd"), QStringLiteral("Q"));
  fiftyYardButton_ = new QPushButton(mainGridWidget);
  configureMainGameControlButton(fiftyYardButton_, QStringLiteral("50-yd"), QStringLiteral("W"));
  seventyFiveYardButton_ = new QPushButton(mainGridWidget);
  configureMainGameControlButton(seventyFiveYardButton_, QStringLiteral("75-yd"), QStringLiteral("E"));
  pcButton_ = new QPushButton(mainGridWidget);
  configureMainGameControlButton(pcButton_, QStringLiteral("PC"), QStringLiteral("R"));

  circleEntryButton_ = new QPushButton(mainGridWidget);
  configureMainGameControlButton(circleEntryButton_, QStringLiteral("Circle Entry"), QStringLiteral("A"));
  pcFoulButton_ = new QPushButton(mainGridWidget);
  configureMainGameControlButton(pcFoulButton_, QStringLiteral("PC Foul"), QStringLiteral("S"));
  shotButton_ = new QPushButton(mainGridWidget);
  configureMainGameControlButton(shotButton_, QStringLiteral("Shot"), QStringLiteral("D"));
  goalButton_ = new QPushButton(mainGridWidget);
  configureMainGameControlButton(goalButton_, QStringLiteral("Goal"), QStringLiteral("F"));

  passButton_ = new QPushButton(mainGridWidget);
  configureMainGameControlButton(passButton_, QStringLiteral("Pass"), QStringLiteral("Z"));
  // Visible title is ☆ in all locales (AppLocale::trEvent); canonical key stays "Special".
  specialButton_ = new QPushButton(mainGridWidget);
  configureMainGameControlButton(specialButton_, QStringLiteral("Special"), QStringLiteral("X"));
  turnoverButton_ = new QPushButton(mainGridWidget);
  configureMainGameControlButton(turnoverButton_, QStringLiteral("Turnover"), QStringLiteral("C"));
  cardButton_ = new QPushButton(mainGridWidget);
  configureMainGameControlButton(cardButton_, QStringLiteral("Card"), QStringLiteral("V"));

  shootoutButton_ = new QPushButton(mainGridWidget);
  configureMainGameControlButton(shootoutButton_, QStringLiteral("S.O."), QStringLiteral("N"));

  psButton_ = new QPushButton(mainGridWidget);
  configureMainGameControlButton(psButton_, QStringLiteral("PS"), QStringLiteral("B"));

  QList<QPushButton*> mainButtons = {
    sixteenYardButton_, fiftyYardButton_, seventyFiveYardButton_, pcButton_,
    circleEntryButton_, pcFoulButton_, shotButton_, goalButton_,
    passButton_, specialButton_, turnoverButton_, cardButton_,
    shootoutButton_, psButton_
  };

  for (auto* button : mainButtons) {
    Style::setSize(button, "md");
    Style::setVariant(button, "gameControl");
    // ClickFocus: not in Tab chain; main actions use Q/W/E… shortcuts and mouse.
    button->setFocusPolicy(Qt::ClickFocus);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setMinimumWidth(0);
    button->setMinimumHeight(52);
  }
  setFocusPolicy(Qt::StrongFocus);

  // Row 0 is QWER. Row 1 (ASDF) shifts ~1/4 key. Row 2 (ZXCV) and B/N shift ~3/4.
  addKeyboardRow(keyboardRowsLayout, mainGridWidget,
                 {sixteenYardButton_, fiftyYardButton_, seventyFiveYardButton_, pcButton_}, 0);
  addKeyboardRow(keyboardRowsLayout, mainGridWidget,
                 {circleEntryButton_, pcFoulButton_, shotButton_, goalButton_}, kHomeRowOffsetUnits);
  addKeyboardRow(keyboardRowsLayout, mainGridWidget,
                 {passButton_, specialButton_, turnoverButton_, cardButton_}, kBottomRowOffsetUnits);
  addKeyboardRow(keyboardRowsLayout, mainGridWidget, {psButton_, shootoutButton_},
                 kBottomRowOffsetUnits);

  // Follow-up buttons container (initially hidden)
  followUpContainer_ = new QWidget(this);
  followUpLayout_ = new QHBoxLayout(followUpContainer_);
  followUpLayout_->setContentsMargins(0, 0, 0, 0);
  followUpLayout_->setSpacing(8);

  mainLayout->addWidget(gameTimeRowWidget);
  mainLayout->addWidget(teamRowWidget);
  mainLayout->addWidget(mainGridWidget);
  mainLayout->addWidget(followUpContainer_);
  mainLayout->addStretch(1);

  const QList<QPushButton*> motionButtons = {
      periodAdvanceButton_, homeTeamButton_, awayTeamButton_,
      sixteenYardButton_, fiftyYardButton_, seventyFiveYardButton_, pcButton_,
      circleEntryButton_, pcFoulButton_, shotButton_, goalButton_,
      passButton_, specialButton_, turnoverButton_, cardButton_,
      shootoutButton_, psButton_};
  for (QPushButton* button : motionButtons) {
    refreshGameButtonMotion(button, activeMainButton_);
  }
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

  auto connectMain = [this](QPushButton* btn) {
    connect(btn, &QPushButton::clicked, this, [this, btn]() { flashButtonBorder(btn); });
    connect(btn, &QPushButton::clicked, this, &GameControls::onMainButtonClicked);
  };
  connectMain(sixteenYardButton_);
  connectMain(fiftyYardButton_);
  connectMain(seventyFiveYardButton_);
  connectMain(pcButton_);
  connectMain(circleEntryButton_);
  connectMain(pcFoulButton_);
  connectMain(shotButton_);
  connectMain(goalButton_);
  connectMain(passButton_);
  connectMain(specialButton_);
  connectMain(turnoverButton_);
  connectMain(cardButton_);
  connectMain(shootoutButton_);
  connectMain(psButton_);
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

  switch (event->key()) {
    case Qt::Key_Q:
      return clickIfReady(sixteenYardButton_);
    case Qt::Key_W:
      return clickIfReady(fiftyYardButton_);
    case Qt::Key_E:
      return clickIfReady(seventyFiveYardButton_);
    case Qt::Key_R:
      return clickIfReady(pcButton_);
    case Qt::Key_A:
      return clickIfReady(circleEntryButton_);
    case Qt::Key_S:
      return clickIfReady(pcFoulButton_);
    case Qt::Key_D:
      return clickIfReady(shotButton_);
    case Qt::Key_F:
      return clickIfReady(goalButton_);
    case Qt::Key_Z:
      return clickIfReady(passButton_);
    case Qt::Key_X:
      return clickIfReady(specialButton_);
    case Qt::Key_C:
      return clickIfReady(turnoverButton_);
    case Qt::Key_V:
      return clickIfReady(cardButton_);
    case Qt::Key_N:
      return clickIfReady(shootoutButton_);
    case Qt::Key_B:
      return clickIfReady(psButton_);
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
  if (!button) return;

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
  followUpState_.beginMainEvent(eventName);
  setActiveMainButton(button);
  emit mainEventTimestampCaptured(eventName);
  const bool chainOpen = !FollowUpCatalog::firstLevelOptions(eventName).isEmpty();
  notifyTagSequence(button->property("shortcutHint").toString(), eventName, chainOpen, true);
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
  const bool chainOpen = !nextOptions.isEmpty();
  notifyTagSequence(QString(), choice, chainOpen, false);
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
  // "Goal" is already the last toast step. A second toast would hide that chain.
  // If Goal ever grows follow-up questions, those still need their own confirmation.
  followUpState_.beginMainEvent(QStringLiteral("Goal"));
  setActiveMainButton(goalButton_);
  emit mainEventTimestampCaptured(QStringLiteral("Goal"));
  const bool chainOpen = !FollowUpCatalog::firstLevelOptions(QStringLiteral("Goal")).isEmpty();
  if (chainOpen) {
    const QString shortcut = goalButton_ ? goalButton_->property("shortcutHint").toString()
                                         : QStringLiteral("F");
    notifyTagSequence(shortcut, QStringLiteral("Goal"), true, true);
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
  Q_ASSERT(!actions.isEmpty());
  hideFollowUpButtons();

  for (int actionIndex = 0; actionIndex < actions.size(); ++actionIndex) {
    const QString& action = actions.at(actionIndex);
    auto* button = new QPushButton(followUpContainer_);
    Style::setSize(button, "md");
    Style::setVariant(button, "gameControlFollowUp");
    configureFollowUpButton(button, action, QString::number(actionIndex + 1));
    button->setFocusPolicy(Qt::ClickFocus);
    button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    button->setMinimumHeight(44);

    connect(button, &QPushButton::clicked, this, [this, button]() { flashButtonBorder(button); });
    connect(button, &QPushButton::clicked, this, &GameControls::onFollowUpButtonClicked);
    refreshGameButtonMotion(button, activeMainButton_);
    button->installEventFilter(this);
    followUpLayout_->addWidget(button, 1);
    followUpButtons_.append(button);
  }

  followUpState_.setStage(stage);
  followUpContainer_->setVisible(true);
  followUpContainer_->update();
}

void GameControls::hideFollowUpButtons() {
  for (auto* button : followUpButtons_) {
    followUpLayout_->removeWidget(button);
    button->deleteLater();
  }
  followUpButtons_.clear();
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
       buildFocusNavEntries(homeTeamButton_, awayTeamButton_, keyboardOrderButtons(), followUpButtons_)) {
    if (entry.button) {
      buttons.append(entry.button);
    }
  }
  return buttons;
}

void GameControls::focusNextInDirection(Qt::Key key) {
  const QVector<FocusNavEntry> entries =
      buildFocusNavEntries(homeTeamButton_, awayTeamButton_, keyboardOrderButtons(), followUpButtons_);
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
  if (event->type() == QEvent::Resize &&
      (obj == homeTeamButton_ || obj == awayTeamButton_)) {
    refreshTeamButtonLabels();
    return false;
  }
  if (event->type() == QEvent::Resize) {
    for (QLabel* titleLabel : mainButtonTitleLabels_) {
      if (titleLabel && titleLabel->parentWidget() == obj) {
        refreshMainButtonTitles();
        break;
      }
    }
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
