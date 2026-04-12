#include "GameSetupWindow.h"
#include "../style/StyleProps.h"
#include "../i18n/AppLocale.h"

#include <QColor>
#include <QColorDialog>
#include <QComboBox>
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
  applyUiStrings();
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
  if (languageCombo_) languageCombo_->setFocus();
}

void GameSetupWindow::applyUiStrings() {
  if (titleLabel_) titleLabel_->setText(AppLocale::trUi("setup.title"));
  if (homeTeamLabel_) homeTeamLabel_->setText(AppLocale::trUi("setup.home_team"));
  if (awayTeamLabel_) awayTeamLabel_->setText(AppLocale::trUi("setup.away_team"));
  if (homeColorLabel_) homeColorLabel_->setText(AppLocale::trUi("setup.home_color"));
  if (awayColorLabel_) awayColorLabel_->setText(AppLocale::trUi("setup.away_color"));
  if (languageLabel_) languageLabel_->setText(AppLocale::trUi("setup.lang_label"));
  if (homeNameEdit_) homeNameEdit_->setPlaceholderText(AppLocale::trUi("setup.placeholder_team"));
  if (awayNameEdit_) awayNameEdit_->setPlaceholderText(AppLocale::trUi("setup.placeholder_team"));
  if (homeColorEdit_) homeColorEdit_->setPlaceholderText(AppLocale::trUi("setup.placeholder_hex"));
  if (awayColorEdit_) awayColorEdit_->setPlaceholderText(AppLocale::trUi("setup.placeholder_hex"));
  if (homeColorButton_) homeColorButton_->setText(AppLocale::trUi("setup.pick"));
  if (awayColorButton_) awayColorButton_->setText(AppLocale::trUi("setup.pick"));
  if (backButton_) backButton_->setText(AppLocale::trUi("setup.back"));
  if (continueButton_) continueButton_->setText(AppLocale::trUi("setup.continue"));
  if (languageCombo_) {
    languageCombo_->blockSignals(true);
    languageCombo_->setItemText(0, AppLocale::trUi("setup.lang_en"));
    languageCombo_->setItemText(1, AppLocale::trUi("setup.lang_es"));
    languageCombo_->setCurrentIndex(AppLocale::currentLanguage() == AppLocale::Language::Spanish ? 1 : 0);
    languageCombo_->blockSignals(false);
  }
}

void GameSetupWindow::onLanguageComboChanged(int index) {
  AppLocale::setLanguage(index == 1 ? AppLocale::Language::Spanish : AppLocale::Language::English);
}

void GameSetupWindow::buildUi() {
  auto* outerLayout = new QVBoxLayout(this);
  outerLayout->setContentsMargins(24, 24, 24, 24);
  outerLayout->addStretch(1);

  auto* contentContainer = new QWidget(this);
  auto* layout = new QVBoxLayout(contentContainer);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(16);

  titleLabel_ = new QLabel(contentContainer);
  titleLabel_->setWordWrap(true);
  titleLabel_->setAlignment(Qt::AlignCenter);
  Style::setRole(titleLabel_, "h1");

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

  auto* languageRow = new QHBoxLayout();
  languageRow->setContentsMargins(0, 0, 0, 0);
  languageRow->setSpacing(8);
  languageLabel_ = new QLabel(contentContainer);
  languageLabel_->setMinimumWidth(100);
  Style::setRole(languageLabel_, "muted");
  languageCombo_ = new QComboBox(contentContainer);
  languageCombo_->addItem(QString());
  languageCombo_->addItem(QString());
  languageCombo_->setMaximumWidth(200);
  languageRow->addWidget(languageLabel_, 0);
  languageRow->addWidget(languageCombo_, 1);
  languageRow->addStretch(1);
  formLayout->addLayout(languageRow);

  homeTeamLabel_ = new QLabel(contentContainer);
  Style::setRole(homeTeamLabel_, "muted");
  homeNameEdit_ = new QLineEdit(contentContainer);
  homeNameEdit_->setMaximumWidth(280);
  addRow(homeTeamLabel_, homeNameEdit_);

  homeColorLabel_ = new QLabel(contentContainer);
  Style::setRole(homeColorLabel_, "muted");
  homeColorLabel_->setMinimumWidth(100);
  auto* homeColorRow = new QWidget(contentContainer);
  auto* homeColorRowLayout = new QHBoxLayout(homeColorRow);
  homeColorRowLayout->setContentsMargins(0, 0, 0, 0);
  homeColorRowLayout->setSpacing(8);
  homeColorEdit_ = new QLineEdit(contentContainer);
  homeColorEdit_->setMaximumWidth(120);
  homeColorButton_ = new QPushButton(contentContainer);
  homeColorButton_->setMinimumWidth(88);
  homeColorRowLayout->addWidget(homeColorEdit_, 0);
  homeColorRowLayout->addWidget(homeColorButton_, 0);
  homeColorRow->setMaximumWidth(280);
  auto* homeColorOuter = new QHBoxLayout();
  homeColorOuter->addWidget(homeColorLabel_);
  homeColorOuter->addWidget(homeColorRow, 1);
  formLayout->addLayout(homeColorOuter);

  awayTeamLabel_ = new QLabel(contentContainer);
  Style::setRole(awayTeamLabel_, "muted");
  awayNameEdit_ = new QLineEdit(contentContainer);
  awayNameEdit_->setMaximumWidth(280);
  addRow(awayTeamLabel_, awayNameEdit_);

  awayColorLabel_ = new QLabel(contentContainer);
  Style::setRole(awayColorLabel_, "muted");
  awayColorLabel_->setMinimumWidth(100);
  auto* awayColorRow = new QWidget(contentContainer);
  auto* awayColorRowLayout = new QHBoxLayout(awayColorRow);
  awayColorRowLayout->setContentsMargins(0, 0, 0, 0);
  awayColorRowLayout->setSpacing(8);
  awayColorEdit_ = new QLineEdit(contentContainer);
  awayColorEdit_->setMaximumWidth(120);
  awayColorButton_ = new QPushButton(contentContainer);
  awayColorButton_->setMinimumWidth(88);
  awayColorRowLayout->addWidget(awayColorEdit_, 0);
  awayColorRowLayout->addWidget(awayColorButton_, 0);
  awayColorRow->setMaximumWidth(280);
  auto* awayColorOuter = new QHBoxLayout();
  awayColorOuter->addWidget(awayColorLabel_);
  awayColorOuter->addWidget(awayColorRow, 1);
  formLayout->addLayout(awayColorOuter);

  layout->addWidget(titleLabel_, 0, Qt::AlignHCenter);
  layout->addLayout(formLayout);

  auto* buttonRow = new QHBoxLayout();
  buttonRow->setSpacing(12);
  backButton_ = new QPushButton(contentContainer);
  backButton_->setCursor(Qt::PointingHandCursor);
  backButton_->setAutoDefault(false);
  backButton_->setDefault(false);
  Style::setVariant(backButton_, "ghost");
  Style::setSize(backButton_, "md");
  continueButton_ = new QPushButton(contentContainer);
  continueButton_->setCursor(Qt::PointingHandCursor);
  continueButton_->setAutoDefault(false);
  continueButton_->setDefault(false);
  Style::setVariant(continueButton_, "welcomeImport");
  Style::setSize(continueButton_, "lg");
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
  connect(languageCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &GameSetupWindow::onLanguageComboChanged);
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
  QColor c = QColorDialog::getColor(current, this, AppLocale::trUi("dialog.pick_home_color"));
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
  QColor c = QColorDialog::getColor(current, this, AppLocale::trUi("dialog.pick_away_color"));
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
