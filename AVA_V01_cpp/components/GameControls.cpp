#include "GameControls.h"
#include "FollowUpCatalog.h"
#include "../style/StyleProps.h"
#include "../i18n/AppLocale.h"
#include "../state/TagSession.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QWidget>
#include <QTimer>
#include <QPointer>
#include <QKeyEvent>
#include <algorithm>
#include <climits>
#include <QEvent>
#include <QApplication>
#include <QLabel>
#include <QColor>

namespace {

constexpr QLatin1StringView kDefaultTeamSelectedBorderHex("#18181b");
constexpr int kFlashDurationMs = 150;

void setButtonFlashState(QPushButton* button, bool flashing) {
  if (!button) {
    return;
  }
  button->setProperty("flash", flashing);
  button->style()->unpolish(button);
  button->style()->polish(button);
  button->update();
}

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

QString teamSelectedButtonStylesheet(const QString& borderHex) {
  const QString borderColor =
      borderHex.isEmpty() ? QString(kDefaultTeamSelectedBorderHex) : borderHex;
  return QStringLiteral(
             "QPushButton {"
             "  border: 2px solid %1;"
             "  border-radius: 6px;"
             "  background: #f4f4f5;"
             "  font-weight: 700;"
             "  color: #09090b;"
             "  padding: 7px 15px;"
             "}"
             "QPushButton:hover {"
             "  background: #e4e4e7;"
             "  border: 2px solid %1;"
             "  border-radius: 6px;"
             "}"
             "QPushButton:focus {"
             "  border: 2px solid %1;"
             "  border-radius: 6px;"
             "  background: #f4f4f5;"
             "  padding: 7px 15px;"
             "}")
      .arg(borderColor);
}

void applyTeamButtonSelectionStyle(QPushButton* button, bool selected, const QString& colorHex) {
  if (!button) return;
  Style::setState(button, "teamSelected", selected);
  if (selected) {
    button->setStyleSheet(teamSelectedButtonStylesheet(normalizeTeamColorHex(colorHex)));
  } else {
    button->setStyleSheet(QString());
  }
}

void configureFollowUpButton(QPushButton* button, const QString& canonicalKey,
                             const QString& shortcutHint) {
  if (!button) return;
  button->setText(QString());
  button->setProperty("gameEventKey", canonicalKey);
  auto* layout = new QVBoxLayout(button);
  layout->setContentsMargins(6, 4, 6, 4);
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

QVector<FocusNavEntry> buildFocusNavEntries(QPushButton* homeTeamButton,
                                            QPushButton* awayTeamButton,
                                            QGridLayout* mainGridLayout,
                                            const QList<QPushButton*>& followUpButtons) {
  QVector<FocusNavEntry> entries;
  if (homeTeamButton) {
    entries.append({homeTeamButton, -1, 0});
  }
  if (awayTeamButton) {
    entries.append({awayTeamButton, -1, 1});
  }
  if (mainGridLayout) {
    for (int row = 0; row < mainGridLayout->rowCount(); ++row) {
      for (int col = 0; col < mainGridLayout->columnCount(); ++col) {
        QLayoutItem* item = mainGridLayout->itemAtPosition(row, col);
        auto* button = qobject_cast<QPushButton*>(item ? item->widget() : nullptr);
        if (button) {
          entries.append({button, row, col});
        }
      }
    }
  }
  const int followUpRow = mainGridLayout ? mainGridLayout->rowCount() : 0;
  for (int index = 0; index < followUpButtons.size(); ++index) {
    QPushButton* button = followUpButtons.at(index);
    if (button && button->isVisible()) {
      entries.append({button, followUpRow, index});
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
      teamSideSelection_(TeamSideSelection::None) {
  setMinimumWidth(kMinimumPanelWidthPx);
  buildUi();
  wireSignals();
  hideFollowUpButtons();
  resetGameTimeState();
  applyUiLanguage();
  installEventFilter(this);
  qApp->installEventFilter(this);
  for (auto* btn : {startGameButton_, nextQuarterButton_, homeTeamButton_, awayTeamButton_,
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
  if (homeTeamButton_) homeTeamButton_->setText(homeTeamFollowUpLabel_);
  if (awayTeamButton_) awayTeamButton_->setText(awayTeamFollowUpLabel_);
  updateTeamButtonSelectionVisual();
}

QString GameControls::selectedTeamLabel() const {
  if (teamSideSelection_ == TeamSideSelection::Home) return homeTeamFollowUpLabel_;
  if (teamSideSelection_ == TeamSideSelection::Away) return awayTeamFollowUpLabel_;
  return QString();
}

void GameControls::updateTeamButtonSelectionVisual() {
  applyTeamButtonSelectionStyle(homeTeamButton_, teamSideSelection_ == TeamSideSelection::Home,
                                homeTeamColorHex_);
  applyTeamButtonSelectionStyle(awayTeamButton_, teamSideSelection_ == TeamSideSelection::Away,
                                awayTeamColorHex_);
}

void GameControls::onHomeTeamButtonClicked() {
  teamSideSelection_ = TeamSideSelection::Home;
  updateTeamButtonSelectionVisual();
  emit teamSideSelected(true);
}

void GameControls::onAwayTeamButtonClicked() {
  teamSideSelection_ = TeamSideSelection::Away;
  updateTeamButtonSelectionVisual();
  emit teamSideSelected(false);
}

void GameControls::setInitialTeamSide(bool selectHome) {
  teamSideSelection_ = selectHome ? TeamSideSelection::Home : TeamSideSelection::Away;
  updateTeamButtonSelectionVisual();
  emit teamSideSelected(selectHome);
}

void GameControls::switchTeamSideToOppositeTeam() {
  if (teamSideSelection_ == TeamSideSelection::Home) {
    onAwayTeamButtonClicked();
    if (awayTeamButton_) {
      awayTeamButton_->setFocus(Qt::TabFocusReason);
      flashButtonBorder(awayTeamButton_);
    }
  } else if (teamSideSelection_ == TeamSideSelection::Away) {
    onHomeTeamButtonClicked();
    if (homeTeamButton_) {
      homeTeamButton_->setFocus(Qt::TabFocusReason);
      flashButtonBorder(homeTeamButton_);
    }
  }
}

QString GameControls::selectedTeamSideKey() const {
  if (teamSideSelection_ == TeamSideSelection::Home) {
    return QStringLiteral("Home");
  }
  if (teamSideSelection_ == TeamSideSelection::Away) {
    return QStringLiteral("Away");
  }
  return QString();
}

void GameControls::applyUiLanguage() {
  for (QLabel* titleLabel : mainButtonTitleLabels_) {
    if (!titleLabel) continue;
    const QString key = titleLabel->property("gameEventName").toString();
    titleLabel->setText(AppLocale::trEvent(key));
  }
  for (QPushButton* followUpButton : followUpButtons_) {
    if (!followUpButton) continue;
    for (QLabel* titleLabel : followUpButton->findChildren<QLabel*>()) {
      const QString key = titleLabel->property("gameEventName").toString();
      if (key.isEmpty()) continue;
      titleLabel->setText(AppLocale::trEvent(key));
    }
  }
  updateGameTimeButtonsUi();
}

void GameControls::resetGameTimeState() {
  gamePhase_ = GamePhase::NotStarted;
  updateGameTimeButtonsUi();
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
  updateGameTimeButtonsUi();
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

void GameControls::updateGameTimeButtonsUi() {
  if (!startGameButton_ || !nextQuarterButton_ || !quarterStatusLabel_) return;

  startGameButton_->setText(AppLocale::trUi("gamecontrols.start_game"));

  switch (gamePhase_) {
    case GamePhase::NotStarted:
      startGameButton_->setEnabled(true);
      nextQuarterButton_->setEnabled(false);
      nextQuarterButton_->setText(AppLocale::trUi("gamecontrols.start_q1"));
      quarterStatusLabel_->setText(AppLocale::trUi("gamecontrols.quarter_not_started"));
      break;
    case GamePhase::Q1:
      startGameButton_->setEnabled(false);
      nextQuarterButton_->setEnabled(true);
      nextQuarterButton_->setText(AppLocale::trUi("gamecontrols.start_q2"));
      quarterStatusLabel_->setText(QStringLiteral("Q1"));
      break;
    case GamePhase::Q2:
      startGameButton_->setEnabled(false);
      nextQuarterButton_->setEnabled(true);
      nextQuarterButton_->setText(AppLocale::trUi("gamecontrols.start_q3"));
      quarterStatusLabel_->setText(QStringLiteral("Q2"));
      break;
    case GamePhase::Q3:
      startGameButton_->setEnabled(false);
      nextQuarterButton_->setEnabled(true);
      nextQuarterButton_->setText(AppLocale::trUi("gamecontrols.start_q4"));
      quarterStatusLabel_->setText(QStringLiteral("Q3"));
      break;
    case GamePhase::Q4:
      startGameButton_->setEnabled(false);
      nextQuarterButton_->setEnabled(true);
      nextQuarterButton_->setText(AppLocale::trUi("gamecontrols.end_game"));
      quarterStatusLabel_->setText(QStringLiteral("Q4"));
      break;
    case GamePhase::Ended:
      startGameButton_->setEnabled(false);
      nextQuarterButton_->setEnabled(false);
      nextQuarterButton_->setText(AppLocale::trUi("gamecontrols.end_game"));
      quarterStatusLabel_->setText(AppLocale::trUi("gamecontrols.quarter_ended"));
      break;
  }
}

void GameControls::onStartGameButtonClicked() {
  if (gamePhase_ != GamePhase::NotStarted) return;
  flashButtonBorder(startGameButton_);
  gamePhase_ = GamePhase::Q1;
  updateGameTimeButtonsUi();
  emit gameStartRequested();
}

void GameControls::onNextQuarterButtonClicked() {
  if (gamePhase_ == GamePhase::NotStarted || gamePhase_ == GamePhase::Ended) return;
  flashButtonBorder(nextQuarterButton_);
  switch (gamePhase_) {
    case GamePhase::Q1: gamePhase_ = GamePhase::Q2; break;
    case GamePhase::Q2: gamePhase_ = GamePhase::Q3; break;
    case GamePhase::Q3: gamePhase_ = GamePhase::Q4; break;
    case GamePhase::Q4: gamePhase_ = GamePhase::Ended; break;
    default: return;
  }
  updateGameTimeButtonsUi();
  emit nextQuarterRequested();
}

void GameControls::setActiveMainButton(QPushButton* button) {
  if (activeMainButton_ == button) return;
  if (activeMainButton_) Style::setState(activeMainButton_, "activeMain", false);
  activeMainButton_ = button;
  if (activeMainButton_) Style::setState(activeMainButton_, "activeMain", true);
}

void GameControls::clearActiveMainButton() {
  if (!activeMainButton_) return;
  Style::setState(activeMainButton_, "activeMain", false);
  activeMainButton_ = nullptr;
}

void GameControls::configureMainGameControlButton(QPushButton* button, const QString& eventName,
                                                  const QString& shortcutHint) {
  if (!button) return;
  button->setText(QString());
  button->setProperty("gameEventName", eventName);
  auto* layout = new QVBoxLayout(button);
  layout->setContentsMargins(8, 6, 8, 6);
  layout->setSpacing(2);
  auto* titleLabel = new QLabel(eventName, button);
  titleLabel->setProperty("gameEventName", eventName);
  mainButtonTitleLabels_.append(titleLabel);
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

void GameControls::buildUi() {
  mainButtonTitleLabels_.clear();
  auto* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(12);

  // Game-time row: Start Game | Next Quarter | quarter status label
  auto* gameTimeRowWidget = new QWidget(this);
  auto* gameTimeRowLayout = new QHBoxLayout(gameTimeRowWidget);
  gameTimeRowLayout->setContentsMargins(0, 0, 0, 0);
  gameTimeRowLayout->setSpacing(8);

  startGameButton_ = new QPushButton(gameTimeRowWidget);
  Style::setVariant(startGameButton_, "gameControl");
  Style::setSize(startGameButton_, "sm");
  startGameButton_->setFocusPolicy(Qt::ClickFocus);
  startGameButton_->setMinimumHeight(40);

  nextQuarterButton_ = new QPushButton(gameTimeRowWidget);
  Style::setVariant(nextQuarterButton_, "gameControl");
  Style::setSize(nextQuarterButton_, "sm");
  nextQuarterButton_->setFocusPolicy(Qt::ClickFocus);
  nextQuarterButton_->setMinimumHeight(40);

  quarterStatusLabel_ = new QLabel(gameTimeRowWidget);
  quarterStatusLabel_->setAlignment(Qt::AlignCenter);
  quarterStatusLabel_->setMinimumWidth(48);
  Style::setRole(quarterStatusLabel_, "h3");

  gameTimeRowLayout->addWidget(startGameButton_, 1);
  gameTimeRowLayout->addWidget(nextQuarterButton_, 1);
  gameTimeRowLayout->addWidget(quarterStatusLabel_, 0);

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
    btn->setMinimumHeight(64);
  }
  teamRowLayout->addWidget(homeTeamButton_, 1);
  teamRowLayout->addWidget(awayTeamButton_, 1);
  setTabOrder(homeTeamButton_, awayTeamButton_);

  // Main grid: rows 0–2 are 4 columns; row 3 is PS + S.O.
  auto* mainGridWidget = new QWidget(this);
  mainGridLayout_ = new QGridLayout(mainGridWidget);
  mainGridLayout_->setContentsMargins(0, 0, 0, 0);
  mainGridLayout_->setSpacing(4);

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
    button->setMinimumHeight(52);
  }
  setFocusPolicy(Qt::StrongFocus);

  // Row 0: 16-yd, 50-yd, 75-yd, PC
  mainGridLayout_->addWidget(sixteenYardButton_, 0, 0);
  mainGridLayout_->addWidget(fiftyYardButton_, 0, 1);
  mainGridLayout_->addWidget(seventyFiveYardButton_, 0, 2);
  mainGridLayout_->addWidget(pcButton_, 0, 3);
  // Row 1: Circle Entry, PC Foul, Shot, Goal
  mainGridLayout_->addWidget(circleEntryButton_, 1, 0);
  mainGridLayout_->addWidget(pcFoulButton_, 1, 1);
  mainGridLayout_->addWidget(shotButton_, 1, 2);
  mainGridLayout_->addWidget(goalButton_, 1, 3);
  // Row 2: Pass, Special, Turnover, Card
  mainGridLayout_->addWidget(passButton_, 2, 0);
  mainGridLayout_->addWidget(specialButton_, 2, 1);
  mainGridLayout_->addWidget(turnoverButton_, 2, 2);
  mainGridLayout_->addWidget(cardButton_, 2, 3);
  // Row 3: PS, S.O.
  mainGridLayout_->addWidget(psButton_, 3, 0);
  mainGridLayout_->addWidget(shootoutButton_, 3, 1);

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
}

void GameControls::wireSignals() {
  if (startGameButton_) {
    connect(startGameButton_, &QPushButton::clicked, this,
            &GameControls::onStartGameButtonClicked);
  }
  if (nextQuarterButton_) {
    connect(nextQuarterButton_, &QPushButton::clicked, this,
            &GameControls::onNextQuarterButtonClicked);
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
      return clickIfReady(startGameButton_);
    case Qt::Key_H:
      return clickIfReady(nextQuarterButton_);
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

  if (teamSideSelection_ == TeamSideSelection::None) {
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
  if (nextOptions.isEmpty()) {
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
  emit tagCommitted(mainEvent, QString());
  clearFollowUpUi();
}

void GameControls::beginChainedGoalFlow() {
  followUpState_.beginMainEvent(QStringLiteral("Goal"));
  setActiveMainButton(goalButton_);
  emit mainEventTimestampCaptured(QStringLiteral("Goal"));
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
    configureFollowUpButton(button, action, QString::number(actionIndex + 1));
    Style::setSize(button, "md");
    Style::setVariant(button, "gameControlFollowUp");
    button->setFocusPolicy(Qt::ClickFocus);
    button->setMinimumHeight(44);

    connect(button, &QPushButton::clicked, this, [this, button]() { flashButtonBorder(button); });
    connect(button, &QPushButton::clicked, this, &GameControls::onFollowUpButtonClicked);
    button->installEventFilter(this);
    followUpLayout_->addWidget(button);
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
  if (!button) {
    return;
  }

  auto* timer = button->findChild<QTimer*>(QStringLiteral("flashClearTimer"), Qt::FindDirectChildrenOnly);
  if (!timer) {
    timer = new QTimer(button);
    timer->setObjectName(QStringLiteral("flashClearTimer"));
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, button, [buttonGuard = QPointer<QPushButton>(button)]() {
      if (!buttonGuard) {
        return;
      }
      setButtonFlashState(buttonGuard, false);
    });
  }

  setButtonFlashState(button, true);
  timer->start(kFlashDurationMs);
}

QList<QPushButton*> GameControls::focusableButtonsOrder() const {
  QList<QPushButton*> buttons;
  for (const FocusNavEntry& entry :
       buildFocusNavEntries(homeTeamButton_, awayTeamButton_, mainGridLayout_, followUpButtons_)) {
    if (entry.button) {
      buttons.append(entry.button);
    }
  }
  return buttons;
}

void GameControls::focusNextInDirection(Qt::Key key) {
  const QVector<FocusNavEntry> entries =
      buildFocusNavEntries(homeTeamButton_, awayTeamButton_, mainGridLayout_, followUpButtons_);
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
