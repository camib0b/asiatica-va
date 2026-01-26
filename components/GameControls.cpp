#include "GameControls.h"
#include "../style/StyleProps.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QWidget>
#include <QTimer>

GameControls::GameControls(QWidget* parent) : QWidget(parent) {
  setMinimumWidth(300);
  buildUi();
  wireSignals();
  hideFollowUpButtons();
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
  enterDButton_ = new QPushButton("Enter D", mainGridWidget);
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

void GameControls::onMainButtonClicked() {
  auto* button = qobject_cast<QPushButton*>(sender());
  if (!button) return;

  QString eventName = button->text();
  currentMainEvent_ = eventName;
  emit mainEventPressed(eventName);
  showFollowUpButtons(eventName);
}

void GameControls::onFollowUpButtonClicked() {
  auto* button = qobject_cast<QPushButton*>(sender());
  if (!button) return;

  QString followUpName = button->text();
  emit gameEventMarked(currentMainEvent_, followUpName);
  hideFollowUpButtons();
  currentMainEvent_.clear();
}

QStringList GameControls::getFollowUpActions(const QString& mainEvent) const {
  if (mainEvent == "Shot") {
    return {"On target", "Off target", "to the Board", "to the Net", "Hit", "Flick"};
  } else if (mainEvent == "Recovery") {
    return {"Interception", "Reception advance", "Tackle", "Block", "Unforced error"};
  } else if (mainEvent == "Enter D") {
    // Placeholder for Enter D follow-up actions
    return {}; // TODO: Add follow-up actions for Enter D
  } else if (mainEvent == "PC") {
    // Placeholder for PC follow-up actions
    return {}; // TODO: Add follow-up actions for PC
  } else if (mainEvent == "Goal") {
    // Placeholder for Goal follow-up actions
    return {}; // TODO: Add follow-up actions for Goal
  } else if (mainEvent == "16-yd hit") {
    // Placeholder for 16-yd hit follow-up actions
    return {}; // TODO: Add follow-up actions for 16-yd hit
  } else if (mainEvent == "50-yd hit") {
    // Placeholder for 50-yd hit follow-up actions
    return {}; // TODO: Add follow-up actions for 50-yd hit
  } else if (mainEvent == "Loss") {
    // Placeholder for Loss follow-up actions
    return {}; // TODO: Add follow-up actions for Loss
  }
  return {};
}

void GameControls::showFollowUpButtons(const QString& mainEvent) {
  // Clear existing follow-up buttons
  hideFollowUpButtons();

  QStringList actions = getFollowUpActions(mainEvent);
  
  // If no follow-up actions, emit the main event directly and return
  if (actions.isEmpty()) {
    emit gameEventMarked(mainEvent);
    currentMainEvent_.clear();
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
