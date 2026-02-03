#include "GameControls.h"
#include "../style/StyleProps.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QWidget>
#include <QTimer>
#include <QAction>
#include <QKeySequence>
#include <QApplication>

GameControls::GameControls(QWidget* parent) : QWidget(parent) {
  setMinimumWidth(300);
  possessionTeam_ = "Home";
  buildUi();
  wireSignals();
  buildKeyboardShortcuts();
  hideFollowUpButtons();
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

void GameControls::buildUi() {
  auto* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(12);

  // Main grid: row 0 = Turnover (full width), rows 1–3 = 3x3 event buttons
  auto* mainGridWidget = new QWidget(this);
  mainGridLayout_ = new QGridLayout(mainGridWidget);
  mainGridLayout_->setContentsMargins(0, 0, 0, 0);
  mainGridLayout_->setSpacing(4);

  turnoverButton_ = new QPushButton("Turnover — Possession: Home", mainGridWidget);
  turnoverButton_->setToolTip("Tab  Toggle possession (Home ↔ Away)");
  Style::setSize(turnoverButton_, "md");
  Style::setVariant(turnoverButton_, "gameControl");
  turnoverButton_->setFocusPolicy(Qt::NoFocus);
  turnoverButton_->setMinimumHeight(40);
  mainGridLayout_->addWidget(turnoverButton_, 0, 0, 1, 3);

  // Create main event buttons
  hit16ydButton_ = new QPushButton("16-yd play", mainGridWidget);
  hit50ydButton_ = new QPushButton("50-yd play", mainGridWidget);
  hit75ydButton_ = new QPushButton("75-yd play", mainGridWidget);
  enterDButton_ = new QPushButton("Circle Entry", mainGridWidget);
  shotButton_ = new QPushButton("Shot", mainGridWidget);
  goalButton_ = new QPushButton("Goal", mainGridWidget);
  pcButton_ = new QPushButton("PC", mainGridWidget);
  psButton_ = new QPushButton("PS", mainGridWidget);
  cardButton_ = new QPushButton("Card", mainGridWidget);

  QList<QPushButton*> mainButtons = {
    hit16ydButton_, hit50ydButton_, hit75ydButton_,
    enterDButton_, shotButton_, goalButton_,
    pcButton_, psButton_, cardButton_
  };

  for (auto* button : mainButtons) {
    Style::setSize(button, "md");
    Style::setVariant(button, "gameControl");
    button->setFocusPolicy(Qt::NoFocus);
    button->setMinimumHeight(40);
  }

  // Row 1: 16-yd play, 50-yd play, 75-yd play
  mainGridLayout_->addWidget(hit16ydButton_, 1, 0);
  mainGridLayout_->addWidget(hit50ydButton_, 1, 1);
  mainGridLayout_->addWidget(hit75ydButton_, 1, 2);
  // Row 2: Circle Entry, Shot, Goal
  mainGridLayout_->addWidget(enterDButton_, 2, 0);
  mainGridLayout_->addWidget(shotButton_, 2, 1);
  mainGridLayout_->addWidget(goalButton_, 2, 2);
  // Row 3: PC, PS, Card
  mainGridLayout_->addWidget(pcButton_, 3, 0);
  mainGridLayout_->addWidget(psButton_, 3, 1);
  mainGridLayout_->addWidget(cardButton_, 3, 2);

  // Follow-up buttons container (initially hidden)
  followUpContainer_ = new QWidget(this);
  followUpLayout_ = new QHBoxLayout(followUpContainer_);
  followUpLayout_->setContentsMargins(0, 0, 0, 0);
  followUpLayout_->setSpacing(8);

  mainLayout->addWidget(mainGridWidget);
  mainLayout->addWidget(followUpContainer_);
  mainLayout->addStretch(1);
}

void GameControls::wireSignals() {
  connect(turnoverButton_, &QPushButton::clicked, this, &GameControls::onTurnoverClicked);

  auto connectMain = [this](QPushButton* btn) {
    connect(btn, &QPushButton::clicked, this, [this, btn]() { flashButtonBorder(btn); });
    connect(btn, &QPushButton::clicked, this, &GameControls::onMainButtonClicked);
  };
  connectMain(hit16ydButton_);
  connectMain(hit50ydButton_);
  connectMain(hit75ydButton_);
  connectMain(enterDButton_);
  connectMain(shotButton_);
  connectMain(goalButton_);
  connectMain(pcButton_);
  connectMain(psButton_);
  connectMain(cardButton_);
}

void GameControls::onTurnoverClicked() {
  possessionTeam_ = (possessionTeam_ == "Home") ? "Away" : "Home";
  turnoverButton_->setText(QString("Turnover — Possession: %1").arg(possessionTeam_));
  emit possessionChanged(possessionTeam_);
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

  // Tab: turnover (toggle possession Home/Away)
  makeAction(Qt::Key_Tab, [this]() {
    if (turnoverButton_ && turnoverButton_->isVisible() && turnoverButton_->isEnabled())
      turnoverButton_->click();
  });

  // Main grid 3x3 left-hand QWERTY: Q W E | A S D | Z X C
  hit16ydAction_ = makeAction(Qt::Key_Q, [this]() {
    if (hit16ydButton_ && hit16ydButton_->isVisible() && hit16ydButton_->isEnabled()) hit16ydButton_->click();
  });
  hit50ydAction_ = makeAction(Qt::Key_W, [this]() {
    if (hit50ydButton_ && hit50ydButton_->isVisible() && hit50ydButton_->isEnabled()) hit50ydButton_->click();
  });
  hit75ydAction_ = makeAction(Qt::Key_E, [this]() {
    if (hit75ydButton_ && hit75ydButton_->isVisible() && hit75ydButton_->isEnabled()) hit75ydButton_->click();
  });
  enterDAction_ = makeAction(Qt::Key_A, [this]() {
    if (enterDButton_ && enterDButton_->isVisible() && enterDButton_->isEnabled()) enterDButton_->click();
  });
  shotAction_ = makeAction(Qt::Key_S, [this]() {
    if (shotButton_ && shotButton_->isVisible() && shotButton_->isEnabled()) shotButton_->click();
  });
  goalAction_ = makeAction(Qt::Key_D, [this]() {
    if (goalButton_ && goalButton_->isVisible() && goalButton_->isEnabled()) goalButton_->click();
  });
  pcAction_ = makeAction(Qt::Key_Z, [this]() {
    if (pcButton_ && pcButton_->isVisible() && pcButton_->isEnabled()) pcButton_->click();
  });
  psAction_ = makeAction(Qt::Key_X, [this]() {
    if (psButton_ && psButton_->isVisible() && psButton_->isEnabled()) psButton_->click();
  });
  cardAction_ = makeAction(Qt::Key_C, [this]() {
    if (cardButton_ && cardButton_->isVisible() && cardButton_->isEnabled()) cardButton_->click();
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
    followUpStage_ = FollowUpStage::None;
  });
}

void GameControls::onMainButtonClicked() {
  auto* button = qobject_cast<QPushButton*>(sender());
  if (!button) return;

  QString eventName = button->text();
  currentMainEvent_ = eventName;
  currentFirstFollowUp_.clear();
  followUpStage_ = FollowUpStage::None;
  setActiveMainButton(button);
  emit mainEventPressed(eventName);
  showFirstLevelFollowUps(eventName);
}

void GameControls::onFollowUpButtonClicked() {
  auto* button = qobject_cast<QPushButton*>(sender());
  if (!button) return;

  QString followUpName = button->text();
  if (followUpStage_ == FollowUpStage::FirstLevel) {
    currentFirstFollowUp_ = followUpName;

    const QStringList second = getSecondLevelFollowUps(currentMainEvent_, currentFirstFollowUp_);
    if (second.isEmpty()) {
      emit gameEventMarked(currentMainEvent_, currentFirstFollowUp_);
      clearActiveMainButton();
      hideFollowUpButtons();
      currentMainEvent_.clear();
      currentFirstFollowUp_.clear();
      followUpStage_ = FollowUpStage::None;
      return;
    }

    showSecondLevelFollowUps(currentMainEvent_, currentFirstFollowUp_);
    return;
  }

  if (followUpStage_ == FollowUpStage::SecondLevel) {
    const QString combined = currentFirstFollowUp_.isEmpty()
      ? followUpName
      : (currentFirstFollowUp_ + " → " + followUpName);

    emit gameEventMarked(currentMainEvent_, combined);
    clearActiveMainButton();
    hideFollowUpButtons();
    currentMainEvent_.clear();
    currentFirstFollowUp_.clear();
    followUpStage_ = FollowUpStage::None;
    return;
  }

  // Fallback: treat as first-level
  emit gameEventMarked(currentMainEvent_, followUpName);
  clearActiveMainButton();
  hideFollowUpButtons();
  currentMainEvent_.clear();
  currentFirstFollowUp_.clear();
  followUpStage_ = FollowUpStage::None;
}

QStringList GameControls::getFirstLevelFollowUps(const QString& mainEvent) const {
  if (mainEvent == "Shot") {
    return {"On target", "Off target"};
  }
  if (mainEvent == "Circle Entry") {
    return {"Dribling", "Pass", "Deflection"};
  }
  if (mainEvent == "PC") {
    return {"Direct shot", "Variant", "Ruined"};
  }
  if (mainEvent == "75-yd play") {
    return {"Forward", "Sideways", "Back"};
  }
  if (mainEvent == "Card") {
    return {"Green", "Yellow", "Red"};
  }
  return {};
}

QStringList GameControls::getSecondLevelFollowUps(const QString& mainEvent, const QString& firstFollowUp) const {
  if (mainEvent == "Shot") {
    if (firstFollowUp == "On target") return {"Goal", "Saved", "Post"};
    if (firstFollowUp == "Off target") return {"Closeby", "Not close"};
    return {};
  }

  if (mainEvent == "PC") {
    if (firstFollowUp == "Direct shot") return {"Hit", "Swept", "Dragflick"};
    return {};
  }

  if (mainEvent == "Circle Entry") {
    // Second-level follow-ups apply to all first-level options (Dribling/Pass/Deflection)
    return {"Left", "Middle", "Right"};
  }

  return {};
}

void GameControls::showFirstLevelFollowUps(const QString& mainEvent) {
  // Clear existing follow-up buttons
  hideFollowUpButtons();

  QStringList actions = getFirstLevelFollowUps(mainEvent);
  
  // If no follow-up actions, emit the main event directly and return
  if (actions.isEmpty()) {
    emit gameEventMarked(mainEvent);
    clearActiveMainButton();
    currentMainEvent_.clear();
    currentFirstFollowUp_.clear();
    followUpStage_ = FollowUpStage::None;
    return;
  }

  // Create new follow-up buttons
  for (const QString& action : actions) {
    auto* button = new QPushButton(action, followUpContainer_);
    Style::setSize(button, "md");
    Style::setVariant(button, "gameControlFollowUp");
    button->setFocusPolicy(Qt::NoFocus);
    button->setMinimumHeight(40);
    
    // Connect click: flash first, then handle
    connect(button, &QPushButton::clicked, this, [this, button]() { flashButtonBorder(button); });
    connect(button, &QPushButton::clicked, this, &GameControls::onFollowUpButtonClicked);
    
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

  QStringList actions = getSecondLevelFollowUps(currentMainEvent_, firstFollowUp);
  if (actions.isEmpty()) {
    // No second-level actions; finalize with the first follow-up
    emit gameEventMarked(currentMainEvent_, firstFollowUp);
    clearActiveMainButton();
    currentMainEvent_.clear();
    currentFirstFollowUp_.clear();
    followUpStage_ = FollowUpStage::None;
    return;
  }

  for (const QString& action : actions) {
    auto* button = new QPushButton(action, followUpContainer_);
    Style::setSize(button, "md");
    Style::setVariant(button, "gameControlFollowUp");
    button->setFocusPolicy(Qt::NoFocus);
    button->setMinimumHeight(40);

    connect(button, &QPushButton::clicked, this, [this, button]() { flashButtonBorder(button); });
    connect(button, &QPushButton::clicked, this, &GameControls::onFollowUpButtonClicked);

    followUpLayout_->addWidget(button);
    followUpButtons_.append(button);
  }

  followUpStage_ = FollowUpStage::SecondLevel;
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
