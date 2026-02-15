#include "GameSetupWindow.h"
#include "../style/StyleProps.h"

#include <QColor>
#include <QColorDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

GameSetupWindow::GameSetupWindow(QWidget* parent) : QWidget(parent) {
  setObjectName("AppRoot");
  setAttribute(Qt::WA_StyledBackground, true);
  buildUi();
  wireSignals();
  setMinimumSize(320, 320);
}

void GameSetupWindow::setVideoPath(const QString& path) {
  videoPath_ = path;
}

void GameSetupWindow::setTeamDefaults(const QString& homeName, const QString& awayName,
                                     const QString& homeColor, const QString& awayColor) {
  if (homeNameEdit_) homeNameEdit_->setText(homeName);
  if (awayNameEdit_) awayNameEdit_->setText(awayName);
  if (homeColorEdit_) homeColorEdit_->setText(homeColor);
  if (awayColorEdit_) awayColorEdit_->setText(awayColor);
}

void GameSetupWindow::setInitialFocus() {
  if (homeNameEdit_) homeNameEdit_->setFocus();
}

void GameSetupWindow::buildUi() {
  auto* outerLayout = new QVBoxLayout(this);
  outerLayout->setContentsMargins(24, 24, 24, 24);
  outerLayout->addStretch(1);

  auto* contentContainer = new QWidget(this);
  auto* layout = new QVBoxLayout(contentContainer);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(16);

  titleLabel_ = new QLabel("Set up teams", contentContainer);
  titleLabel_->setWordWrap(true);
  titleLabel_->setAlignment(Qt::AlignCenter);
  Style::setRole(titleLabel_, "h1");

  subtitleLabel_ = new QLabel("Enter team names and colors for this session.", contentContainer);
  subtitleLabel_->setWordWrap(true);
  subtitleLabel_->setAlignment(Qt::AlignCenter);
  Style::setRole(subtitleLabel_, "subhero");

  auto* formLayout = new QVBoxLayout();
  formLayout->setSpacing(12);

  auto addRow = [&formLayout](QLabel* label, QWidget* widget) {
    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    label->setMinimumWidth(100);
    row->addWidget(label);
    row->addWidget(widget, 1);
    formLayout->addLayout(row);
  };

  auto* homeNameLabel = new QLabel("Home team:", contentContainer);
  Style::setRole(homeNameLabel, "muted");
  homeNameEdit_ = new QLineEdit(contentContainer);
  homeNameEdit_->setPlaceholderText("e.g. Lakers");
  homeNameEdit_->setMaximumWidth(280);
  addRow(homeNameLabel, homeNameEdit_);

  auto* homeColorLabel = new QLabel("Home color:", contentContainer);
  Style::setRole(homeColorLabel, "muted");
  homeColorLabel->setMinimumWidth(100);
  auto* homeColorRow = new QWidget(contentContainer);
  auto* homeColorRowLayout = new QHBoxLayout(homeColorRow);
  homeColorRowLayout->setContentsMargins(0, 0, 0, 0);
  homeColorRowLayout->setSpacing(8);
  homeColorEdit_ = new QLineEdit(contentContainer);
  homeColorEdit_->setPlaceholderText("#RRGGBB");
  homeColorEdit_->setMaximumWidth(120);
  homeColorButton_ = new QPushButton("Pick", contentContainer);
  homeColorButton_->setFixedWidth(56);
  homeColorRowLayout->addWidget(homeColorEdit_, 0);
  homeColorRowLayout->addWidget(homeColorButton_, 0);
  homeColorRow->setMaximumWidth(280);
  auto* homeColorOuter = new QHBoxLayout();
  homeColorOuter->addWidget(homeColorLabel);
  homeColorOuter->addWidget(homeColorRow, 1);
  formLayout->addLayout(homeColorOuter);

  auto* awayNameLabel = new QLabel("Away team:", contentContainer);
  Style::setRole(awayNameLabel, "muted");
  awayNameEdit_ = new QLineEdit(contentContainer);
  awayNameEdit_->setPlaceholderText("e.g. Celtics");
  awayNameEdit_->setMaximumWidth(280);
  addRow(awayNameLabel, awayNameEdit_);

  auto* awayColorLabel = new QLabel("Away color:", contentContainer);
  Style::setRole(awayColorLabel, "muted");
  awayColorLabel->setMinimumWidth(100);
  auto* awayColorRow = new QWidget(contentContainer);
  auto* awayColorRowLayout = new QHBoxLayout(awayColorRow);
  awayColorRowLayout->setContentsMargins(0, 0, 0, 0);
  awayColorRowLayout->setSpacing(8);
  awayColorEdit_ = new QLineEdit(contentContainer);
  awayColorEdit_->setPlaceholderText("#RRGGBB");
  awayColorEdit_->setMaximumWidth(120);
  awayColorButton_ = new QPushButton("Pick", contentContainer);
  awayColorButton_->setFixedWidth(56);
  awayColorRowLayout->addWidget(awayColorEdit_, 0);
  awayColorRowLayout->addWidget(awayColorButton_, 0);
  awayColorRow->setMaximumWidth(280);
  auto* awayColorOuter = new QHBoxLayout();
  awayColorOuter->addWidget(awayColorLabel);
  awayColorOuter->addWidget(awayColorRow, 1);
  formLayout->addLayout(awayColorOuter);

  layout->addWidget(titleLabel_, 0, Qt::AlignHCenter);
  layout->addWidget(subtitleLabel_, 0, Qt::AlignHCenter);
  layout->addLayout(formLayout);

  auto* buttonRow = new QHBoxLayout();
  buttonRow->setSpacing(12);
  backButton_ = new QPushButton("&Back", contentContainer);
  backButton_->setCursor(Qt::PointingHandCursor);
  backButton_->setAutoDefault(false);
  backButton_->setDefault(false);
  Style::setVariant(backButton_, "ghost");
  Style::setSize(backButton_, "md");
  continueButton_ = new QPushButton("&Continue", contentContainer);
  continueButton_->setCursor(Qt::PointingHandCursor);
  continueButton_->setAutoDefault(false);
  continueButton_->setDefault(false);
  Style::setVariant(continueButton_, "welcomeImport");
  Style::setSize(continueButton_, "lg");
  continueButton_->setMaximumWidth(220);
  buttonRow->addStretch(1);
  buttonRow->addWidget(backButton_, 0);
  buttonRow->addWidget(continueButton_, 0);
  buttonRow->addStretch(1);
  layout->addLayout(buttonRow);

  outerLayout->addWidget(contentContainer, 0, Qt::AlignCenter);
  outerLayout->addStretch(1);
}

void GameSetupWindow::wireSignals() {
  connect(continueButton_, &QPushButton::clicked, this, &GameSetupWindow::onContinue);
  connect(backButton_, &QPushButton::clicked, this, &GameSetupWindow::onBack);
  connect(homeColorButton_, &QPushButton::clicked, this, &GameSetupWindow::onHomeColorPick);
  connect(awayColorButton_, &QPushButton::clicked, this, &GameSetupWindow::onAwayColorPick);
}

void GameSetupWindow::onContinue() {
  const QString homeName = homeNameEdit_ ? homeNameEdit_->text().trimmed() : QString();
  const QString awayName = awayNameEdit_ ? awayNameEdit_->text().trimmed() : QString();
  const QString homeColor = homeColorEdit_ ? homeColorEdit_->text().trimmed() : QString();
  const QString awayColor = awayColorEdit_ ? awayColorEdit_->text().trimmed() : QString();
  emit teamSetupConfirmed(videoPath_, homeName, awayName, homeColor, awayColor);
}

void GameSetupWindow::onBack() {
  emit cancelled();
}

void GameSetupWindow::onHomeColorPick() {
  QColor current;
  QString hex = homeColorEdit_ ? homeColorEdit_->text().trimmed() : QString();
  if (!hex.isEmpty() && hex.startsWith('#')) {
    current = QColor(hex);
    if (!current.isValid()) current = QColor(Qt::blue);
  } else {
    current = QColor(Qt::blue);
  }
  QColor c = QColorDialog::getColor(current, this, "Home team color");
  if (c.isValid() && homeColorEdit_) {
    homeColorEdit_->setText(colorToHex(c));
  }
}

void GameSetupWindow::onAwayColorPick() {
  QColor current;
  QString hex = awayColorEdit_ ? awayColorEdit_->text().trimmed() : QString();
  if (!hex.isEmpty() && hex.startsWith('#')) {
    current = QColor(hex);
    if (!current.isValid()) current = QColor(Qt::red);
  } else {
    current = QColor(Qt::red);
  }
  QColor c = QColorDialog::getColor(current, this, "Away team color");
  if (c.isValid() && awayColorEdit_) {
    awayColorEdit_->setText(colorToHex(c));
  }
}

QString GameSetupWindow::colorToHex(const QColor& c) {
  return QString("#%1%2%3")
      .arg(c.red(), 2, 16, QChar('0'))
      .arg(c.green(), 2, 16, QChar('0'))
      .arg(c.blue(), 2, 16, QChar('0'));
}
