#include "GameSetupWindow.h"

#include "../components/TeamColorPicker.h"
#include "../i18n/AppLocale.h"
#include "../style/StyleProps.h"

#include <QComboBox>
#include <QColor>
#include <QDate>
#include <QDateEdit>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QWidget>

GameSetupWindow::GameSetupWindow(QWidget* parent) : QWidget(parent) {
  setObjectName("AppRoot");
  setAttribute(Qt::WA_StyledBackground, true);
  buildUi();
  wireSignals();
  applyUiStrings();
  updateOptionalFieldAppearance();
  setMinimumSize(480, 520);
}

void GameSetupWindow::setVideoPath(const QString& path) {
  videoPath_ = path;
}

void GameSetupWindow::setTeamDefaults(const QString& homeName, const QString& awayName,
                                     const QString& homeColor, const QString& awayColor) {
  if (homeNameEdit_) homeNameEdit_->setText(homeName);
  if (awayNameEdit_) awayNameEdit_->setText(awayName);
  if (homeColorPicker_) homeColorPicker_->setColor(homeColor);
  if (awayColorPicker_) awayColorPicker_->setColor(awayColor);
}

void GameSetupWindow::setMetadataDefaults(const QString& competitionName,
                                          const QDate& gameDate,
                                          const QString& homeAbbrev,
                                          const QString& awayAbbrev) {
  dateEditedByUser_ = false;
  if (competitionEdit_) competitionEdit_->setText(competitionName);
  if (gameDateEdit_) {
    ignoreDateChange_ = true;
    gameDateEdit_->setDate(gameDate.isValid() ? gameDate : QDate::currentDate());
    ignoreDateChange_ = false;
  }
  if (homeAbbrevEdit_) homeAbbrevEdit_->setText(homeAbbrev);
  if (awayAbbrevEdit_) awayAbbrevEdit_->setText(awayAbbrev);
  updateOptionalFieldAppearance();
}

void GameSetupWindow::setInitialFocus() {
  if (homeNameEdit_) homeNameEdit_->setFocus();
}

void GameSetupWindow::applyUiStrings() {
  if (titleLabel_) titleLabel_->setText(AppLocale::trUi("setup.title"));
  if (homeTeamLabel_) homeTeamLabel_->setText(AppLocale::trUi("setup.home_team"));
  if (awayTeamLabel_) awayTeamLabel_->setText(AppLocale::trUi("setup.away_team"));
  if (optionalLabel_) optionalLabel_->setText(AppLocale::trUi("setup.optional"));
  if (competitionLabel_) competitionLabel_->setText(AppLocale::trUi("setup.competition"));
  if (dateLabel_) dateLabel_->setText(AppLocale::trUi("setup.date"));
  if (homeNameEdit_) homeNameEdit_->setPlaceholderText(AppLocale::trUi("setup.placeholder_home_team"));
  if (awayNameEdit_) awayNameEdit_->setPlaceholderText(AppLocale::trUi("setup.placeholder_away_team"));
  if (homeAbbrevEdit_) homeAbbrevEdit_->setPlaceholderText(AppLocale::trUi("setup.placeholder_abbrev"));
  if (awayAbbrevEdit_) awayAbbrevEdit_->setPlaceholderText(AppLocale::trUi("setup.placeholder_abbrev"));
  if (competitionEdit_) {
    competitionEdit_->setPlaceholderText(AppLocale::trUi("setup.placeholder_competition"));
  }
  if (backButton_) backButton_->setText(AppLocale::trUi("setup.back"));
  if (continueButton_) continueButton_->setText(AppLocale::trUi("setup.continue"));
  if (languageCombo_) {
    languageCombo_->blockSignals(true);
    languageCombo_->setItemText(0, AppLocale::trUi("setup.lang_en"));
    languageCombo_->setItemText(1, AppLocale::trUi("setup.lang_es"));
    languageCombo_->setCurrentIndex(AppLocale::currentLanguage() == AppLocale::Language::Spanish ? 1 : 0);
    languageCombo_->blockSignals(false);
    const QString languageLabel = AppLocale::trUi("setup.lang_label");
    languageCombo_->setToolTip(languageLabel);
    languageCombo_->setAccessibleName(languageLabel);
  }
  if (homeColorPicker_) {
    homeColorPicker_->setColorDialogTitle(AppLocale::trUi("dialog.pick_home_color"));
    homeColorPicker_->applyUiStrings();
  }
  if (awayColorPicker_) {
    awayColorPicker_->setColorDialogTitle(AppLocale::trUi("dialog.pick_away_color"));
    awayColorPicker_->applyUiStrings();
  }
}

void GameSetupWindow::onLanguageComboChanged(int index) {
  AppLocale::setLanguage(index == 1 ? AppLocale::Language::Spanish : AppLocale::Language::English);
}

QString GameSetupWindow::deriveAbbreviationFromTeamName(const QString& teamName) {
  QString collected;
  collected.reserve(3);
  for (QChar character : teamName) {
    if (character.isLetterOrNumber()) {
      collected.append(character.toUpper());
      if (collected.size() == 3) break;
    }
  }
  return collected;
}

void GameSetupWindow::onHomeNameEditingFinished() {
  if (!homeAbbrevEdit_ || !homeNameEdit_) return;
  if (!homeAbbrevEdit_->text().trimmed().isEmpty()) return;
  const QString derived = deriveAbbreviationFromTeamName(homeNameEdit_->text());
  if (!derived.isEmpty()) {
    QSignalBlocker blocker(homeAbbrevEdit_);
    homeAbbrevEdit_->setText(derived);
  }
}

void GameSetupWindow::onAwayNameEditingFinished() {
  if (!awayAbbrevEdit_ || !awayNameEdit_) return;
  if (!awayAbbrevEdit_->text().trimmed().isEmpty()) return;
  const QString derived = deriveAbbreviationFromTeamName(awayNameEdit_->text());
  if (!derived.isEmpty()) {
    QSignalBlocker blocker(awayAbbrevEdit_);
    awayAbbrevEdit_->setText(derived);
  }
}

void GameSetupWindow::onCompetitionTextChanged(const QString&) {
  updateOptionalFieldAppearance();
}

void GameSetupWindow::onGameDateChanged(QDate) {
  if (ignoreDateChange_) return;
  dateEditedByUser_ = true;
  updateOptionalFieldAppearance();
}

void GameSetupWindow::updateOptionalFieldAppearance() {
  const bool competitionActive =
      competitionEdit_ && !competitionEdit_->text().trimmed().isEmpty();
  Style::setProp(competitionEdit_, "optionalState", competitionActive ? "active" : "stale");
  Style::setProp(competitionLabel_, "optionalState", competitionActive ? "active" : "stale");

  const char* dateState = dateEditedByUser_ ? "active" : "stale";
  Style::setProp(gameDateEdit_, "optionalState", dateState);
  Style::setProp(dateLabel_, "optionalState", dateState);
}

void GameSetupWindow::buildUi() {
  auto* outerLayout = new QVBoxLayout(this);
  outerLayout->setContentsMargins(24, 16, 24, 24);
  outerLayout->setSpacing(0);

  auto* headerRow = new QHBoxLayout();
  headerRow->setContentsMargins(0, 0, 0, 0);
  languageCombo_ = new QComboBox(this);
  languageCombo_->setObjectName(QStringLiteral("GameSetupLanguageCombo"));
  languageCombo_->addItem(QString());
  languageCombo_->addItem(QString());
  languageCombo_->setMaximumWidth(140);
  languageCombo_->setFocusPolicy(Qt::ClickFocus);
  Style::setVariant(languageCombo_, "compact");
  headerRow->addStretch(1);
  headerRow->addWidget(languageCombo_, 0, Qt::AlignTop);
  outerLayout->addLayout(headerRow);

  outerLayout->addStretch(1);

  auto* contentContainer = new QWidget(this);
  contentContainer->setMinimumWidth(440);
  contentContainer->setMaximumWidth(520);
  auto* layout = new QVBoxLayout(contentContainer);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(20);

  titleLabel_ = new QLabel(contentContainer);
  titleLabel_->setWordWrap(true);
  titleLabel_->setAlignment(Qt::AlignCenter);
  Style::setRole(titleLabel_, "h1");
  layout->addWidget(titleLabel_, 0, Qt::AlignHCenter);

  auto addTeamGroup = [&](QLabel*& teamLabel, QLineEdit*& nameEdit, QLineEdit*& abbrevEdit,
                          TeamColorPicker*& colorPicker, bool homeSide) {
    auto* group = new QWidget(contentContainer);
    auto* groupLayout = new QVBoxLayout(group);
    groupLayout->setContentsMargins(0, 0, 0, 0);
    groupLayout->setSpacing(8);

    teamLabel = new QLabel(group);
    Style::setRole(teamLabel, "h3");
    groupLayout->addWidget(teamLabel);

    auto* fieldsRow = new QHBoxLayout();
    fieldsRow->setContentsMargins(0, 0, 0, 0);
    fieldsRow->setSpacing(8);

    nameEdit = new QLineEdit(group);
    abbrevEdit = new QLineEdit(group);
    abbrevEdit->setMaxLength(3);
    abbrevEdit->setMaximumWidth(80);
    abbrevEdit->setMinimumWidth(64);

    colorPicker = new TeamColorPicker(group);
    colorPicker->setFallbackPreviewColor(homeSide ? QColor(Qt::blue) : QColor(Qt::red));

    fieldsRow->addWidget(nameEdit, 1);
    fieldsRow->addWidget(abbrevEdit, 0);
    fieldsRow->addWidget(colorPicker, 0, Qt::AlignVCenter);
    groupLayout->addLayout(fieldsRow);
    layout->addWidget(group);
  };

  addTeamGroup(homeTeamLabel_, homeNameEdit_, homeAbbrevEdit_, homeColorPicker_, true);
  addTeamGroup(awayTeamLabel_, awayNameEdit_, awayAbbrevEdit_, awayColorPicker_, false);

  optionalLabel_ = new QLabel(contentContainer);
  Style::setRole(optionalLabel_, "faint");
  layout->addWidget(optionalLabel_);

  auto* optionalBlock = new QWidget(contentContainer);
  optionalBlock->setObjectName(QStringLiteral("OptionalSetupFields"));
  auto* optionalLayout = new QVBoxLayout(optionalBlock);
  optionalLayout->setContentsMargins(0, 0, 0, 0);
  optionalLayout->setSpacing(8);

  auto addOptionalRow = [&](QLabel*& label, QWidget* field) {
    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);
    label = new QLabel(optionalBlock);
    label->setMinimumWidth(96);
    Style::setRole(label, "faint");
    row->addWidget(label, 0);
    row->addWidget(field, 1);
    optionalLayout->addLayout(row);
  };

  competitionEdit_ = new QLineEdit(optionalBlock);
  addOptionalRow(competitionLabel_, competitionEdit_);

  gameDateEdit_ = new QDateEdit(QDate::currentDate(), optionalBlock);
  gameDateEdit_->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
  gameDateEdit_->setCalendarPopup(true);
  gameDateEdit_->setMaximumWidth(180);
  addOptionalRow(dateLabel_, gameDateEdit_);

  layout->addWidget(optionalBlock);

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

  setTabOrder(homeNameEdit_, homeAbbrevEdit_);
  setTabOrder(homeAbbrevEdit_, homeColorPicker_);
  setTabOrder(homeColorPicker_, awayNameEdit_);
  setTabOrder(awayNameEdit_, awayAbbrevEdit_);
  setTabOrder(awayAbbrevEdit_, awayColorPicker_);
  setTabOrder(awayColorPicker_, competitionEdit_);
  setTabOrder(competitionEdit_, gameDateEdit_);
  setTabOrder(gameDateEdit_, continueButton_);
  setTabOrder(continueButton_, backButton_);
}

void GameSetupWindow::wireSignals() {
  connect(continueButton_, &QPushButton::clicked, this, &GameSetupWindow::onContinue);
  connect(backButton_, &QPushButton::clicked, this, &GameSetupWindow::onBack);
  connect(languageCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &GameSetupWindow::onLanguageComboChanged);
  connect(homeNameEdit_, &QLineEdit::editingFinished, this,
          &GameSetupWindow::onHomeNameEditingFinished);
  connect(awayNameEdit_, &QLineEdit::editingFinished, this,
          &GameSetupWindow::onAwayNameEditingFinished);
  connect(competitionEdit_, &QLineEdit::textChanged, this,
          &GameSetupWindow::onCompetitionTextChanged);
  connect(gameDateEdit_, &QDateEdit::dateChanged, this, &GameSetupWindow::onGameDateChanged);
}

void GameSetupWindow::onContinue() {
  const QString homeName = homeNameEdit_ ? homeNameEdit_->text().trimmed() : QString();
  const QString awayName = awayNameEdit_ ? awayNameEdit_->text().trimmed() : QString();
  const QString homeColor = homeColorPicker_ ? homeColorPicker_->color() : QString();
  const QString awayColor = awayColorPicker_ ? awayColorPicker_->color() : QString();
  const QString competitionName = competitionEdit_ ? competitionEdit_->text().trimmed() : QString();
  const QDate gameDate = gameDateEdit_ ? gameDateEdit_->date() : QDate();

  QString homeAbbrev = homeAbbrevEdit_ ? homeAbbrevEdit_->text().trimmed().toUpper() : QString();
  QString awayAbbrev = awayAbbrevEdit_ ? awayAbbrevEdit_->text().trimmed().toUpper() : QString();
  if (homeAbbrev.isEmpty()) {
    homeAbbrev = deriveAbbreviationFromTeamName(homeName);
  }
  if (awayAbbrev.isEmpty()) {
    awayAbbrev = deriveAbbreviationFromTeamName(awayName);
  }

  emit teamSetupConfirmed(videoPath_, homeName, awayName, homeColor, awayColor,
                          competitionName, gameDate, homeAbbrev, awayAbbrev);
}

void GameSetupWindow::onBack() {
  emit cancelled();
}
