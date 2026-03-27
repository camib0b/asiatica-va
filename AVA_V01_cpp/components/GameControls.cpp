#include "GameControls.h"
#include "../style/StyleProps.h"
#include "../i18n/AppLocale.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QWidget>
#include <QTimer>
#include <QAction>
#include <QKeySequence>
#include <QKeyEvent>
#include <QEvent>
#include <QApplication>
#include <QLabel>

namespace {
void setupFollowUpButton(QPushButton* button, const QString& canonicalKey) {
  if (!button) return;
  button->setProperty("gameEventKey", canonicalKey);
  button->setText(AppLocale::trEvent(canonicalKey));
}
} // namespace

GameControls::GameControls(QWidget* parent) : QWidget(parent) {
  setMinimumWidth(300);
  buildUi();
  wireSignals();
  buildKeyboardShortcuts();
  hideFollowUpButtons();
  applyUiLanguage();
  installEventFilter(this);
  for (auto* btn : {homeTeamButton_, awayTeamButton_, hit16ydButton_, hit50ydButton_, hit75ydButton_,
                    pcButton_, enterDButton_, pcFoulButton_, shotButton_, goalButton_, passButton_,
                    specialButton_, turnoverButton_, cardButton_, psButton_})
    if (btn) btn->installEventFilter(this);
}

void GameControls::setSessionTeamNames(const QString& homeName, const QString& awayName) {
  const QString homeTrimmed = homeName.trimmed();
  const QString awayTrimmed = awayName.trimmed();
  homeTeamFollowUpLabel_ = homeTrimmed.isEmpty() ? QStringLiteral("home") : homeTrimmed;
  awayTeamFollowUpLabel_ = awayTrimmed.isEmpty() ? QStringLiteral("away") : awayTrimmed;
  teamSideSelection_ = TeamSideSelection::None;
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
  if (homeTeamButton_)
    Style::setState(homeTeamButton_, "teamSelected", teamSideSelection_ == TeamSideSelection::Home);
  if (awayTeamButton_)
    Style::setState(awayTeamButton_, "teamSelected", teamSideSelection_ == TeamSideSelection::Away);
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

  // Main grid: rows 0–2 are 4 columns; row 3 is PS only (column 0)
  auto* mainGridWidget = new QWidget(this);
  mainGridLayout_ = new QGridLayout(mainGridWidget);
  mainGridLayout_->setContentsMargins(0, 0, 0, 0);
  mainGridLayout_->setSpacing(4);

  // Create main event buttons (title + keyboard hint on second line)
  hit16ydButton_ = new QPushButton(mainGridWidget);
  configureMainGameControlButton(hit16ydButton_, QStringLiteral("16-yd play"), QStringLiteral("Q"));
  hit50ydButton_ = new QPushButton(mainGridWidget);
  configureMainGameControlButton(hit50ydButton_, QStringLiteral("50-yd play"), QStringLiteral("W"));
  hit75ydButton_ = new QPushButton(mainGridWidget);
  configureMainGameControlButton(hit75ydButton_, QStringLiteral("75-yd play"), QStringLiteral("E"));
  pcButton_ = new QPushButton(mainGridWidget);
  configureMainGameControlButton(pcButton_, QStringLiteral("PC"), QStringLiteral("R"));

  enterDButton_ = new QPushButton(mainGridWidget);
  configureMainGameControlButton(enterDButton_, QStringLiteral("Circle Entry"), QStringLiteral("A"));
  pcFoulButton_ = new QPushButton(mainGridWidget);
  configureMainGameControlButton(pcFoulButton_, QStringLiteral("PC Foul"), QStringLiteral("S"));
  shotButton_ = new QPushButton(mainGridWidget);
  configureMainGameControlButton(shotButton_, QStringLiteral("Shot"), QStringLiteral("D"));
  goalButton_ = new QPushButton(mainGridWidget);
  configureMainGameControlButton(goalButton_, QStringLiteral("Goal"), QStringLiteral("F"));

  passButton_ = new QPushButton(mainGridWidget);
  configureMainGameControlButton(passButton_, QStringLiteral("Pass"), QStringLiteral("Z"));
  specialButton_ = new QPushButton(mainGridWidget);
  configureMainGameControlButton(specialButton_, QStringLiteral("Special"), QStringLiteral("X"));
  turnoverButton_ = new QPushButton(mainGridWidget);
  configureMainGameControlButton(turnoverButton_, QStringLiteral("Turnover"), QStringLiteral("C"));
  cardButton_ = new QPushButton(mainGridWidget);
  configureMainGameControlButton(cardButton_, QStringLiteral("Card"), QStringLiteral("V"));

  psButton_ = new QPushButton(mainGridWidget);
  configureMainGameControlButton(psButton_, QStringLiteral("PS"), QStringLiteral("B"));

  QList<QPushButton*> mainButtons = {
    hit16ydButton_, hit50ydButton_, hit75ydButton_, pcButton_,
    enterDButton_, pcFoulButton_, shotButton_, goalButton_,
    passButton_, specialButton_, turnoverButton_, cardButton_,
    psButton_
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
  mainGridLayout_->addWidget(hit16ydButton_, 0, 0);
  mainGridLayout_->addWidget(hit50ydButton_, 0, 1);
  mainGridLayout_->addWidget(hit75ydButton_, 0, 2);
  mainGridLayout_->addWidget(pcButton_, 0, 3);
  // Row 1: Circle Entry, PC Foul, Shot, Goal
  mainGridLayout_->addWidget(enterDButton_, 1, 0);
  mainGridLayout_->addWidget(pcFoulButton_, 1, 1);
  mainGridLayout_->addWidget(shotButton_, 1, 2);
  mainGridLayout_->addWidget(goalButton_, 1, 3);
  // Row 2: Pass, Special, Turnover, Card
  mainGridLayout_->addWidget(passButton_, 2, 0);
  mainGridLayout_->addWidget(specialButton_, 2, 1);
  mainGridLayout_->addWidget(turnoverButton_, 2, 2);
  mainGridLayout_->addWidget(cardButton_, 2, 3);
  // Row 3: PS
  mainGridLayout_->addWidget(psButton_, 3, 0);

  // Follow-up buttons container (initially hidden)
  followUpContainer_ = new QWidget(this);
  followUpLayout_ = new QHBoxLayout(followUpContainer_);
  followUpLayout_->setContentsMargins(0, 0, 0, 0);
  followUpLayout_->setSpacing(8);

  mainLayout->addWidget(teamRowWidget);
  mainLayout->addWidget(mainGridWidget);
  mainLayout->addWidget(followUpContainer_);
  mainLayout->addStretch(1);
}

void GameControls::wireSignals() {
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
  connectMain(hit16ydButton_);
  connectMain(hit50ydButton_);
  connectMain(hit75ydButton_);
  connectMain(pcButton_);
  connectMain(enterDButton_);
  connectMain(pcFoulButton_);
  connectMain(shotButton_);
  connectMain(goalButton_);
  connectMain(passButton_);
  connectMain(specialButton_);
  connectMain(turnoverButton_);
  connectMain(cardButton_);
  connectMain(psButton_);
}

void GameControls::buildKeyboardShortcuts() {
  Q_ASSERT(QApplication::instance() != nullptr);

  auto makeAction = [this](int key, auto handler) -> QAction* {
    auto* act = new QAction(this);
    act->setShortcut(QKeySequence(key));
    act->setShortcutContext(Qt::ApplicationShortcut);
    connect(act, &QAction::triggered, this, handler);
    this->addAction(act);
    return act;
  };

  // Main grid matches QWERTY geometry: Q W E R | A S D F | Z X C V | B (PS)
  hit16ydAction_ = makeAction(Qt::Key_Q, [this]() {
    if (hit16ydButton_ && hit16ydButton_->isVisible() && hit16ydButton_->isEnabled()) hit16ydButton_->click();
  });
  hit50ydAction_ = makeAction(Qt::Key_W, [this]() {
    if (hit50ydButton_ && hit50ydButton_->isVisible() && hit50ydButton_->isEnabled()) hit50ydButton_->click();
  });
  hit75ydAction_ = makeAction(Qt::Key_E, [this]() {
    if (hit75ydButton_ && hit75ydButton_->isVisible() && hit75ydButton_->isEnabled()) hit75ydButton_->click();
  });
  pcAction_ = makeAction(Qt::Key_R, [this]() {
    if (pcButton_ && pcButton_->isVisible() && pcButton_->isEnabled()) pcButton_->click();
  });
  enterDAction_ = makeAction(Qt::Key_A, [this]() {
    if (enterDButton_ && enterDButton_->isVisible() && enterDButton_->isEnabled()) enterDButton_->click();
  });
  pcFoulAction_ = makeAction(Qt::Key_S, [this]() {
    if (pcFoulButton_ && pcFoulButton_->isVisible() && pcFoulButton_->isEnabled()) pcFoulButton_->click();
  });
  shotAction_ = makeAction(Qt::Key_D, [this]() {
    if (shotButton_ && shotButton_->isVisible() && shotButton_->isEnabled()) shotButton_->click();
  });
  goalAction_ = makeAction(Qt::Key_F, [this]() {
    if (goalButton_ && goalButton_->isVisible() && goalButton_->isEnabled()) goalButton_->click();
  });
  passAction_ = makeAction(Qt::Key_Z, [this]() {
    if (passButton_ && passButton_->isVisible() && passButton_->isEnabled()) passButton_->click();
  });
  specialAction_ = makeAction(Qt::Key_X, [this]() {
    if (specialButton_ && specialButton_->isVisible() && specialButton_->isEnabled()) specialButton_->click();
  });
  turnoverAction_ = makeAction(Qt::Key_C, [this]() {
    if (turnoverButton_ && turnoverButton_->isVisible() && turnoverButton_->isEnabled()) turnoverButton_->click();
  });
  cardAction_ = makeAction(Qt::Key_V, [this]() {
    if (cardButton_ && cardButton_->isVisible() && cardButton_->isEnabled()) cardButton_->click();
  });
  psAction_ = makeAction(Qt::Key_B, [this]() {
    if (psButton_ && psButton_->isVisible() && psButton_->isEnabled()) psButton_->click();
  });

  // Follow-ups: map visible follow-up buttons to 1..9
  followUpNumberActions_.clear();
  for (int i = 1; i <= 9; ++i) {
    const int key = (i == 1) ? Qt::Key_1
                  : (i == 2) ? Qt::Key_2
                  : (i == 3) ? Qt::Key_3
                  : (i == 4) ? Qt::Key_4
                  : (i == 5) ? Qt::Key_5
                  : (i == 6) ? Qt::Key_6
                  : (i == 7) ? Qt::Key_7
                  : (i == 8) ? Qt::Key_8
                             : Qt::Key_9;

    auto* act = makeAction(key, [this, i]() {
      if (!followUpContainer_ || !followUpContainer_->isVisible()) return;
      const int idx = i - 1;
      if (idx < 0 || idx >= followUpButtons_.size()) return;
      auto* btn = followUpButtons_.at(idx);
      if (!btn || !btn->isVisible() || !btn->isEnabled()) return;
      btn->click();
    });
    followUpNumberActions_.append(act);
  }

  // Escape: discard follow-ups and save with empty follow-up
  escapeAction_ = makeAction(Qt::Key_Escape, [this]() {
    if (followUpStage_ == FollowUpStage::None || currentMainEvent_.isEmpty()) return;

    // Emit with empty follow-up
    emit gameEventMarked(currentMainEvent_, QString());
    clearActiveMainButton();
    hideFollowUpButtons();
    currentMainEvent_.clear();
    currentFirstFollowUp_.clear();
    currentSecondFollowUp_.clear();
    followUpStage_ = FollowUpStage::None;
  });
}

void GameControls::onMainButtonClicked() {
  auto* button = qobject_cast<QPushButton*>(sender());
  if (!button) return;

  if (teamSideSelection_ == TeamSideSelection::None) {
    flashButtonBorder(homeTeamButton_);
    flashButtonBorder(awayTeamButton_);
    return;
  }

  QString eventName = button->property("gameEventName").toString();
  if (eventName.isEmpty()) {
    eventName = button->text();
  }
  currentMainEvent_ = eventName;
  currentFirstFollowUp_.clear();
  currentSecondFollowUp_.clear();
  followUpStage_ = FollowUpStage::None;
  setActiveMainButton(button);
  emit mainEventPressed(eventName);
  showFirstLevelFollowUps(eventName);
}

void GameControls::onFollowUpButtonClicked() {
  auto* button = qobject_cast<QPushButton*>(sender());
  if (!button) return;

  QString followUpName = button->property("gameEventKey").toString();
  if (followUpName.isEmpty()) followUpName = button->text();
  if (followUpStage_ == FollowUpStage::FirstLevel) {
    currentFirstFollowUp_ = followUpName;

    const QStringList second = getSecondLevelFollowUps(currentMainEvent_, currentFirstFollowUp_);
    if (second.isEmpty()) {
      if (currentMainEvent_ == QStringLiteral("PS")) {
        const QString payload = selectedTeamLabel() + QStringLiteral(" → ") + currentFirstFollowUp_;
        emit gameEventMarked(QStringLiteral("PS"), payload);
        if (currentFirstFollowUp_ == QStringLiteral("Goal")) {
          currentMainEvent_ = QStringLiteral("Goal");
          currentFirstFollowUp_.clear();
          currentSecondFollowUp_.clear();
          setActiveMainButton(goalButton_);
          emit mainEventPressed(QStringLiteral("Goal"));
          showFirstLevelFollowUps(QStringLiteral("Goal"));
          return;
        }
        clearActiveMainButton();
        hideFollowUpButtons();
        currentMainEvent_.clear();
        currentFirstFollowUp_.clear();
        currentSecondFollowUp_.clear();
        followUpStage_ = FollowUpStage::None;
        return;
      }

      QString followUpPayload;
      if (currentMainEvent_ == QStringLiteral("PC Foul")) {
        followUpPayload = selectedTeamLabel() + QStringLiteral(" → ") + currentFirstFollowUp_;
      } else if (currentMainEvent_ == QStringLiteral("Card")) {
        followUpPayload = currentFirstFollowUp_ + QStringLiteral(" → ") + selectedTeamLabel();
      } else if (currentMainEvent_ == QStringLiteral("75-yd play")) {
        followUpPayload = currentFirstFollowUp_ + QStringLiteral(" → ") + selectedTeamLabel();
      } else {
        followUpPayload = currentFirstFollowUp_;
      }
      emit gameEventMarked(currentMainEvent_, followUpPayload);
      if (currentMainEvent_ == QStringLiteral("Turnover") && !followUpPayload.isEmpty()) {
        switchTeamSideToOppositeTeam();
      }
      clearActiveMainButton();
      hideFollowUpButtons();
      currentMainEvent_.clear();
      currentFirstFollowUp_.clear();
      currentSecondFollowUp_.clear();
      followUpStage_ = FollowUpStage::None;
      return;
    }

    showSecondLevelFollowUps(currentMainEvent_, currentFirstFollowUp_);
    return;
  }

  if (followUpStage_ == FollowUpStage::SecondLevel) {
    if (currentMainEvent_ == QStringLiteral("Circle Entry")) {
      const QString combined = selectedTeamLabel() + QStringLiteral(" → ") + currentFirstFollowUp_ +
                               QStringLiteral(" → ") + followUpName;
      emit gameEventMarked(currentMainEvent_, combined);
      clearActiveMainButton();
      hideFollowUpButtons();
      currentMainEvent_.clear();
      currentFirstFollowUp_.clear();
      currentSecondFollowUp_.clear();
      followUpStage_ = FollowUpStage::None;
      return;
    }

    if (currentMainEvent_ == QStringLiteral("PC")) {
      if (currentFirstFollowUp_ == QStringLiteral("Direct shot")) {
        currentSecondFollowUp_ = followUpName;
        const QStringList third = getThirdLevelFollowUps(currentMainEvent_, currentFirstFollowUp_, currentSecondFollowUp_);
        if (!third.isEmpty()) {
          showThirdLevelFollowUps();
          return;
        }
      }

      const QString combined = selectedTeamLabel() + QStringLiteral(" → ") + currentFirstFollowUp_ +
                               QStringLiteral(" → ") + followUpName;
      emit gameEventMarked(currentMainEvent_, combined);

      if (followUpName == QStringLiteral("Goal")) {
        currentMainEvent_ = QStringLiteral("Goal");
        currentFirstFollowUp_.clear();
        currentSecondFollowUp_.clear();
        setActiveMainButton(goalButton_);
        emit mainEventPressed(QStringLiteral("Goal"));
        showFirstLevelFollowUps(QStringLiteral("Goal"));
        return;
      }

      clearActiveMainButton();
      hideFollowUpButtons();
      currentMainEvent_.clear();
      currentFirstFollowUp_.clear();
      currentSecondFollowUp_.clear();
      followUpStage_ = FollowUpStage::None;
      return;
    }

    // Shot → On target → Goal matches the main "Goal" control: continue with Goal follow-ups.
    // Register the completed shot path first (same payload as the generic second-level combine),
    // then emit Goal so WorkWindow records Shot then Goal without dropping the shot.
    if (currentMainEvent_ == QStringLiteral("Shot") &&
        currentFirstFollowUp_ == QStringLiteral("On target") &&
        followUpName == QStringLiteral("Goal")) {
      const QString shotOutcome =
          currentFirstFollowUp_ + QStringLiteral(" → ") + followUpName;
      emit gameEventMarked(QStringLiteral("Shot"), shotOutcome);
      currentMainEvent_ = QStringLiteral("Goal");
      currentFirstFollowUp_.clear();
      currentSecondFollowUp_.clear();
      setActiveMainButton(goalButton_);
      emit mainEventPressed(QStringLiteral("Goal"));
      showFirstLevelFollowUps(QStringLiteral("Goal"));
      return;
    }

    const QString combined = currentFirstFollowUp_.isEmpty()
      ? followUpName
      : (currentFirstFollowUp_ + " → " + followUpName);

    emit gameEventMarked(currentMainEvent_, combined);
    clearActiveMainButton();
    hideFollowUpButtons();
    currentMainEvent_.clear();
    currentFirstFollowUp_.clear();
    currentSecondFollowUp_.clear();
    followUpStage_ = FollowUpStage::None;
    return;
  }

  if (followUpStage_ == FollowUpStage::ThirdLevel) {
    if (currentMainEvent_ == QStringLiteral("PC")) {
      const QString pcOutcome = selectedTeamLabel() + QStringLiteral(" → ") +
                                currentFirstFollowUp_ + QStringLiteral(" → ") +
                                currentSecondFollowUp_ + QStringLiteral(" → ") + followUpName;
      emit gameEventMarked(QStringLiteral("PC"), pcOutcome);

      if (followUpName == QStringLiteral("Goal")) {
        currentMainEvent_ = QStringLiteral("Goal");
        currentFirstFollowUp_.clear();
        currentSecondFollowUp_.clear();
        setActiveMainButton(goalButton_);
        emit mainEventPressed(QStringLiteral("Goal"));
        showFirstLevelFollowUps(QStringLiteral("Goal"));
        return;
      }

      clearActiveMainButton();
      hideFollowUpButtons();
      currentMainEvent_.clear();
      currentFirstFollowUp_.clear();
      currentSecondFollowUp_.clear();
      followUpStage_ = FollowUpStage::None;
      return;
    }

    const QString combined = currentFirstFollowUp_ + " → " + currentSecondFollowUp_ + " → " + followUpName;
    emit gameEventMarked(currentMainEvent_, combined);
    clearActiveMainButton();
    hideFollowUpButtons();
    currentMainEvent_.clear();
    currentFirstFollowUp_.clear();
    currentSecondFollowUp_.clear();
    followUpStage_ = FollowUpStage::None;
    return;
  }

  // Fallback: treat as first-level
  emit gameEventMarked(currentMainEvent_, followUpName);
  clearActiveMainButton();
  hideFollowUpButtons();
  currentMainEvent_.clear();
  currentFirstFollowUp_.clear();
  currentSecondFollowUp_.clear();
  followUpStage_ = FollowUpStage::None;
}

QStringList GameControls::getFirstLevelFollowUps(const QString& mainEvent) const {
  if (mainEvent == "Shot") {
    return {QStringLiteral("On target"), QStringLiteral("Off target"), QStringLiteral("Blocked")};
  }
  if (mainEvent == "Circle Entry") {
    return {"Dribling", "Pass", "Deflection"};
  }
  if (mainEvent == "PC") {
    return {QStringLiteral("Direct shot"), QStringLiteral("Variant"), QStringLiteral("Ruined")};
  }
  if (mainEvent == "16-yd play" || mainEvent == "50-yd play") {
    return {};
  }
  if (mainEvent == "75-yd play") {
    return {"Forward", "Sideways", "Back"};
  }
  if (mainEvent == "Goal") {
    return {};
  }
  if (mainEvent == "Card") {
    return {"Green", "Yellow", "Red"};
  }
  if (mainEvent == "Pass") {
    return {"Flick", "Push", "Sweep", "Hit"};
  }
  if (mainEvent == "Special") {
    return {"Good", "Bad", "Neutral", "Referee"};
  }
  if (mainEvent == "Turnover") {
    return {"Interception", "Tackle", "Pressure", "Unforced error"};
  }
  if (mainEvent == "PC Foul") {
    return {"Foot", "Stick", "Danger", "Other"};
  }
  if (mainEvent == "PS") {
    return {QStringLiteral("Goal"), QStringLiteral("No Goal")};
  }
  return {};
}

QStringList GameControls::getSecondLevelFollowUps(const QString& mainEvent, const QString& firstFollowUp) const {
  if (mainEvent == "16-yd play" || mainEvent == "50-yd play") {
    return {};
  }
  if (mainEvent == "Goal") {
    return {};
  }
  if (mainEvent == "Card") {
    return {};
  }
  if (mainEvent == "75-yd play") {
    if (firstFollowUp == "Forward" || firstFollowUp == "Sideways" || firstFollowUp == "Back") {
      return {};
    }
    return {};
  }
  if (mainEvent == "Shot") {
    if (firstFollowUp == QStringLiteral("On target")) return {QStringLiteral("Goal"), QStringLiteral("Saved"), QStringLiteral("Post")};
    if (firstFollowUp == QStringLiteral("Off target")) return {QStringLiteral("Closeby"), QStringLiteral("Not close")};
    if (firstFollowUp == QStringLiteral("Blocked")) return {};
    return {};
  }

  if (mainEvent == "PC") {
    if (firstFollowUp == QStringLiteral("Direct shot")) {
      return {QStringLiteral("Hit"), QStringLiteral("Swept"), QStringLiteral("Dragflick")};
    }
    if (firstFollowUp == QStringLiteral("Variant") || firstFollowUp == QStringLiteral("Ruined")) {
      return {QStringLiteral("Goal"), QStringLiteral("No Goal")};
    }
    return {};
  }

  if (mainEvent == "Circle Entry") {
    if (firstFollowUp == "Dribling" || firstFollowUp == "Pass" || firstFollowUp == "Deflection") {
      return {"Left", "Middle", "Right"};
    }
    return {};
  }

  if (mainEvent == "Pass") {
    if (firstFollowUp == "Flick" || firstFollowUp == "Push" || firstFollowUp == "Sweep" ||
        firstFollowUp == "Hit") {
      return {"Completed", "Failed"};
    }
    return {};
  }

  if (mainEvent == "Special") {
    return {};
  }

  if (mainEvent == "PC Foul") {
    return {};
  }

  return {};
}

QStringList GameControls::getThirdLevelFollowUps(const QString& mainEvent, const QString& firstFollowUp,
                                                 const QString& secondFollowUp) const {
  if (mainEvent == QStringLiteral("PC") && firstFollowUp == QStringLiteral("Direct shot")) {
    if (secondFollowUp == QStringLiteral("Hit") || secondFollowUp == QStringLiteral("Swept") ||
        secondFollowUp == QStringLiteral("Dragflick")) {
      return {QStringLiteral("Goal"), QStringLiteral("No Goal")};
    }
  }
  return {};
}

void GameControls::showFirstLevelFollowUps(const QString& mainEvent) {
  // Clear existing follow-up buttons
  hideFollowUpButtons();

  QStringList actions = getFirstLevelFollowUps(mainEvent);

  // If no follow-up actions, emit the main event directly and return
  if (actions.isEmpty()) {
    if (mainEvent == QStringLiteral("16-yd play") || mainEvent == QStringLiteral("50-yd play") ||
        mainEvent == QStringLiteral("Goal")) {
      emit gameEventMarked(mainEvent, selectedTeamLabel());
    } else {
      emit gameEventMarked(mainEvent);
    }
    clearActiveMainButton();
    currentMainEvent_.clear();
    currentFirstFollowUp_.clear();
    currentSecondFollowUp_.clear();
    followUpStage_ = FollowUpStage::None;
    return;
  }

  // Create new follow-up buttons
  for (const QString& action : actions) {
    auto* button = new QPushButton(followUpContainer_);
    setupFollowUpButton(button, action);
    Style::setSize(button, "md");
    Style::setVariant(button, "gameControlFollowUp");
    button->setFocusPolicy(Qt::ClickFocus);
    button->setMinimumHeight(40);

    // Connect click: flash first, then handle
    connect(button, &QPushButton::clicked, this, [this, button]() { flashButtonBorder(button); });
    connect(button, &QPushButton::clicked, this, &GameControls::onFollowUpButtonClicked);
    button->installEventFilter(this);
    followUpLayout_->addWidget(button);
    followUpButtons_.append(button);
  }

  followUpStage_ = FollowUpStage::FirstLevel;
  followUpContainer_->setVisible(true);
  followUpContainer_->update();
}

void GameControls::showSecondLevelFollowUps(const QString& mainEvent, const QString& firstFollowUp) {
  Q_UNUSED(mainEvent);

  // Clear existing follow-up buttons and rebuild
  hideFollowUpButtons();
  currentSecondFollowUp_.clear();

  QStringList actions = getSecondLevelFollowUps(currentMainEvent_, firstFollowUp);
  if (actions.isEmpty()) {
    QString combined;
    if (currentMainEvent_ == QStringLiteral("75-yd play")) {
      combined = firstFollowUp + QStringLiteral(" → ") + selectedTeamLabel();
    } else if (currentMainEvent_ == QStringLiteral("Card")) {
      combined = firstFollowUp + QStringLiteral(" → ") + selectedTeamLabel();
    } else {
      combined = firstFollowUp;
    }
    emit gameEventMarked(currentMainEvent_, combined);
    clearActiveMainButton();
    currentMainEvent_.clear();
    currentFirstFollowUp_.clear();
    currentSecondFollowUp_.clear();
    followUpStage_ = FollowUpStage::None;
    return;
  }

  for (const QString& action : actions) {
    auto* button = new QPushButton(followUpContainer_);
    setupFollowUpButton(button, action);
    Style::setSize(button, "md");
    Style::setVariant(button, "gameControlFollowUp");
    button->setFocusPolicy(Qt::ClickFocus);
    button->setMinimumHeight(40);

    connect(button, &QPushButton::clicked, this, [this, button]() { flashButtonBorder(button); });
    connect(button, &QPushButton::clicked, this, &GameControls::onFollowUpButtonClicked);
    button->installEventFilter(this);
    followUpLayout_->addWidget(button);
    followUpButtons_.append(button);
  }

  followUpStage_ = FollowUpStage::SecondLevel;
  followUpContainer_->setVisible(true);
  followUpContainer_->update();
}

void GameControls::showThirdLevelFollowUps() {
  hideFollowUpButtons();

  QStringList actions =
      getThirdLevelFollowUps(currentMainEvent_, currentFirstFollowUp_, currentSecondFollowUp_);
  if (actions.isEmpty()) {
    const QString combined = currentFirstFollowUp_.isEmpty()
      ? currentSecondFollowUp_
      : (currentFirstFollowUp_ + " → " + currentSecondFollowUp_);
    emit gameEventMarked(currentMainEvent_, combined);
    clearActiveMainButton();
    currentMainEvent_.clear();
    currentFirstFollowUp_.clear();
    currentSecondFollowUp_.clear();
    followUpStage_ = FollowUpStage::None;
    return;
  }

  for (const QString& action : actions) {
    auto* button = new QPushButton(followUpContainer_);
    setupFollowUpButton(button, action);
    Style::setSize(button, "md");
    Style::setVariant(button, "gameControlFollowUp");
    button->setFocusPolicy(Qt::ClickFocus);
    button->setMinimumHeight(40);

    connect(button, &QPushButton::clicked, this, [this, button]() { flashButtonBorder(button); });
    connect(button, &QPushButton::clicked, this, &GameControls::onFollowUpButtonClicked);
    button->installEventFilter(this);
    followUpLayout_->addWidget(button);
    followUpButtons_.append(button);
  }

  followUpStage_ = FollowUpStage::ThirdLevel;
  followUpContainer_->setVisible(true);
  followUpContainer_->update();
}

void GameControls::hideFollowUpButtons() {
  // Remove and delete all follow-up buttons
  for (auto* button : followUpButtons_) {
    // Clean up any flash timers for this button
    if (flashTimers_.contains(button)) {
      QTimer* timer = flashTimers_.take(button);
      if (timer) {
        timer->stop();
        timer->deleteLater();
      }
    }
    followUpLayout_->removeWidget(button);
    button->deleteLater();
  }
  followUpButtons_.clear();
  followUpContainer_->setVisible(false);
}

void GameControls::flashButtonBorder(QPushButton* button) {
  if (!button) return;

  QTimer* timer = flashTimers_.value(button, nullptr);
  if (!timer) {
    timer = new QTimer(button);
    timer->setSingleShot(true);
    flashTimers_.insert(button, timer);

    connect(timer, &QTimer::timeout, this, [button]() {
      if (!button) return;
      button->setProperty("flash", false);
      button->style()->unpolish(button);
      button->style()->polish(button);
      button->update();
    });
  }

  button->setProperty("flash", true);
  button->style()->unpolish(button);
  button->style()->polish(button);
  button->update();

  timer->start(150);
}

QList<QPushButton*> GameControls::focusableButtonsOrder() const {
  QList<QPushButton*> list;
  list << homeTeamButton_ << awayTeamButton_
       << hit16ydButton_ << hit50ydButton_ << hit75ydButton_ << pcButton_
       << enterDButton_ << pcFoulButton_ << shotButton_ << goalButton_
       << passButton_ << specialButton_ << turnoverButton_ << cardButton_
       << psButton_;
  for (auto* btn : followUpButtons_) {
    if (btn && btn->isVisible()) list << btn;
  }
  return list;
}

void GameControls::focusNextInDirection(Qt::Key key) {
  QList<QPushButton*> list = focusableButtonsOrder();
  if (list.isEmpty()) return;

  QWidget* focus = focusWidget();
  int idx = -1;
  for (int i = 0; i < list.size(); ++i) {
    if (list.at(i) == focus) { idx = i; break; }
  }
  if (idx < 0) {
    list.first()->setFocus(Qt::OtherFocusReason);
    return;
  }

  // Indices 0–1: team row; 2–14: main grid (13 buttons, PS at 14); follow-ups after that
  const int kTeamCount = 2;
  const int kMainStart = 2;
  const int kMainGridCols = 4;
  const int kNumMainButtons = 13;
  const int kPsButtonIndex = kMainStart + 12;
  const int kFirstFollowUpIndex = kTeamCount + kNumMainButtons;
  int next = idx;

  if (key == Qt::Key_Right) {
    next = (idx + 1) % list.size();
  } else if (key == Qt::Key_Left) {
    next = (idx - 1 + list.size()) % list.size();
  } else if (key == Qt::Key_Down) {
    if (idx == 0) {
      next = kMainStart;
    } else if (idx == 1) {
      next = kMainStart + 1;
    } else if (idx >= kMainStart && idx < kMainStart + 12) {
      const int mainIdx = idx - kMainStart;
      const int row = mainIdx / kMainGridCols;
      if (row < 2) {
        next = idx + kMainGridCols;
      } else {
        next = kPsButtonIndex;
      }
    } else if (idx == kPsButtonIndex) {
      next = (list.size() > kFirstFollowUpIndex) ? kFirstFollowUpIndex : kMainStart;
    } else {
      next = (idx + 1) % list.size();
    }
  } else if (key == Qt::Key_Up) {
    if (idx >= kMainStart && idx <= kMainStart + 3) {
      if (idx == kMainStart)
        next = 0;
      else if (idx == kMainStart + 1)
        next = 1;
      else if (idx == kMainStart + 2 || idx == kMainStart + 3)
        next = 1;
    } else if (idx >= kMainStart + 4 && idx < kPsButtonIndex) {
      next = idx - kMainGridCols;
    } else if (idx == kPsButtonIndex) {
      next = kMainStart + 8;
    } else if (idx == kFirstFollowUpIndex) {
      next = kPsButtonIndex;
    } else if (idx > kFirstFollowUpIndex) {
      next = 0;
    } else {
      next = (idx - 1 + list.size()) % list.size();
    }
  }

  if (next < 0) next = 0;
  if (next >= list.size()) next = list.size() - 1;
  if (QPushButton* btn = list.value(next))
    btn->setFocus(Qt::TabFocusReason);
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
      if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter || keyEvent->key() == Qt::Key_Space) {
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
  if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space) {
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
