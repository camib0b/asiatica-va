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

  // Main grid of 8 buttons (2 rows x 4 columns)
  auto* mainGridWidget = new QWidget(this);
  mainGridLayout_ = new QGridLayout(mainGridWidget);
  mainGridLayout_->setContentsMargins(0, 0, 0, 0);
  mainGridLayout_->setSpacing(4);

  // Create main buttons
  enterDButton_ = new QPushButton("Circle Entry", mainGridWidget);
  shotButton_ = new QPushButton("Shot", mainGridWidget);
  pcButton_ = new QPushButton("PC", mainGridWidget);
  goalButton_ = new QPushButton("Goal", mainGridWidget);
  hit16ydButton_ = new QPushButton("16-yd hit", mainGridWidget);
  hit50ydButton_ = new QPushButton("50-yd hit", mainGridWidget);
  recoveryButton_ = new QPushButton("Recovery", mainGridWidget);
  lossButton_ = new QPushButton("Loss", mainGridWidget);

  // Style all main buttons
  QList<QPushButton*> mainButtons = {
    enterDButton_, shotButton_, pcButton_, goalButton_,
    hit16ydButton_, hit50ydButton_, recoveryButton_, lossButton_
  };

  for (auto* button : mainButtons) {
    Style::setSize(button, "md");
    Style::setVariant(button, "outline");
    button->setFocusPolicy(Qt::NoFocus);
    button->setMinimumHeight(40);
  }

  // Arrange buttons in grid (2 rows x 4 columns)
  mainGridLayout_->addWidget(enterDButton_, 0, 0);
  mainGridLayout_->addWidget(shotButton_, 0, 1);
  mainGridLayout_->addWidget(pcButton_, 0, 2);
  mainGridLayout_->addWidget(goalButton_, 0, 3);
  mainGridLayout_->addWidget(hit16ydButton_, 1, 0);
  mainGridLayout_->addWidget(hit50ydButton_, 1, 1);
  mainGridLayout_->addWidget(recoveryButton_, 1, 2);
  mainGridLayout_->addWidget(lossButton_, 1, 3);

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
  // Main buttons: flash on click, then handle the click
  connect(enterDButton_, &QPushButton::clicked, this, [this]() { flashButtonBorder(enterDButton_); });
  connect(enterDButton_, &QPushButton::clicked, this, &GameControls::onMainButtonClicked);
  
  connect(shotButton_, &QPushButton::clicked, this, [this]() { flashButtonBorder(shotButton_); });
  connect(shotButton_, &QPushButton::clicked, this, &GameControls::onMainButtonClicked);
  
  connect(pcButton_, &QPushButton::clicked, this, [this]() { flashButtonBorder(pcButton_); });
  connect(pcButton_, &QPushButton::clicked, this, &GameControls::onMainButtonClicked);
  
  connect(goalButton_, &QPushButton::clicked, this, [this]() { flashButtonBorder(goalButton_); });
  connect(goalButton_, &QPushButton::clicked, this, &GameControls::onMainButtonClicked);
  
  connect(hit16ydButton_, &QPushButton::clicked, this, [this]() { flashButtonBorder(hit16ydButton_); });
  connect(hit16ydButton_, &QPushButton::clicked, this, &GameControls::onMainButtonClicked);
  
  connect(hit50ydButton_, &QPushButton::clicked, this, [this]() { flashButtonBorder(hit50ydButton_); });
  connect(hit50ydButton_, &QPushButton::clicked, this, &GameControls::onMainButtonClicked);
  
  connect(recoveryButton_, &QPushButton::clicked, this, [this]() { flashButtonBorder(recoveryButton_); });
  connect(recoveryButton_, &QPushButton::clicked, this, &GameControls::onMainButtonClicked);
  
  connect(lossButton_, &QPushButton::clicked, this, [this]() { flashButtonBorder(lossButton_); });
  connect(lossButton_, &QPushButton::clicked, this, &GameControls::onMainButtonClicked);
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

  // Main grid: Q W E R / A S D F
  enterDAction_ = makeAction(Qt::Key_Q, [this]() {
    if (enterDButton_ && enterDButton_->isVisible() && enterDButton_->isEnabled()) enterDButton_->click();
  });
  shotAction_ = makeAction(Qt::Key_W, [this]() {
    if (shotButton_ && shotButton_->isVisible() && shotButton_->isEnabled()) shotButton_->click();
  });
  pcAction_ = makeAction(Qt::Key_E, [this]() {
    if (pcButton_ && pcButton_->isVisible() && pcButton_->isEnabled()) pcButton_->click();
  });
  goalAction_ = makeAction(Qt::Key_R, [this]() {
    if (goalButton_ && goalButton_->isVisible() && goalButton_->isEnabled()) goalButton_->click();
  });

  hit16ydAction_ = makeAction(Qt::Key_A, [this]() {
    if (hit16ydButton_ && hit16ydButton_->isVisible() && hit16ydButton_->isEnabled()) hit16ydButton_->click();
  });
  hit50ydAction_ = makeAction(Qt::Key_S, [this]() {
    if (hit50ydButton_ && hit50ydButton_->isVisible() && hit50ydButton_->isEnabled()) hit50ydButton_->click();
  });
  recoveryAction_ = makeAction(Qt::Key_D, [this]() {
    if (recoveryButton_ && recoveryButton_->isVisible() && recoveryButton_->isEnabled()) recoveryButton_->click();
  });
  lossAction_ = makeAction(Qt::Key_F, [this]() {
    if (lossButton_ && lossButton_->isVisible() && lossButton_->isEnabled()) lossButton_->click();
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
    return {"Left", "Middle", "Right"};
  }
  if (mainEvent == "PC") {
    return {"Direct shot", "Variant", "Ruined"};
  }
  if (mainEvent == "Recovery") {
    return {"Interception", "Reception advance", "Tackle", "Block", "Unforced error"};
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
    // Second-level follow-ups apply to all first-level options (Left/Middle/Right)
    return {"Dribling", "Pass", "Deflection"};
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
    Style::setVariant(button, "secondary");
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
    Style::setVariant(button, "secondary");
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
