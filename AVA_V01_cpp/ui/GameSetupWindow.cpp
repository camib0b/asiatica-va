#include "GameSetupWindow.h"
#include "QtPtr.h"

#include "../components/TeamColorPicker.h"
#include "../export/GameMetadataSuggester.h"
#include "../export/XaiConfig.h"
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

GameSetupWindow::GameSetupWindow(QWidget* parent)
    : QWidget(parent),
      videoPath_(),
      optionalDateChosen_(false),
      suggestionSignalsArmed_(false),
      suggestionStatusKey_(nullptr),
      metadataSuggester_(std::make_unique<GameMetadataSuggester>()),
      metadataSuggesterConnections_(),
      titleLabel_(nullptr),
      suggestionStatusLabel_(nullptr),
      homeTeamLabel_(nullptr),
      awayTeamLabel_(nullptr),
      optionalLabel_(nullptr),
      dateLabel_(nullptr),
      homeNameEdit_(nullptr),
      awayNameEdit_(nullptr),
      homeAbbrevEdit_(nullptr),
      awayAbbrevEdit_(nullptr),
      gameDateEdit_(nullptr),
      homeColorPicker_(nullptr),
      awayColorPicker_(nullptr),
      languageCombo_(nullptr),
      continueButton_(nullptr),
      backButton_(nullptr) {
  setObjectName("AppRoot");
  setAttribute(Qt::WA_StyledBackground, true);
  buildUi();
  wireSignals();
  applyUiStrings();
  applyOptionalDate(QDate::currentDate(), OptionalDateCommit::Placeholder);
  setMinimumSize(480, 520);
}

GameSetupWindow::~GameSetupWindow() {
  discardMetadataSuggester();
}

void GameSetupWindow::setVideoPath(const QString& path) {
  videoPath_ = path;
  updateContinueButtonEnabled();
}

void GameSetupWindow::setTeamDefaults(const QString& homeName, const QString& awayName,
                                     const QString& homeColor, const QString& awayColor) {
  if (homeNameEdit_) homeNameEdit_->setText(homeName);
  if (awayNameEdit_) awayNameEdit_->setText(awayName);
  if (homeColorPicker_) homeColorPicker_->setColor(homeColor);
  if (awayColorPicker_) awayColorPicker_->setColor(awayColor);
  updateContinueButtonEnabled();
}

void GameSetupWindow::setMetadataDefaults(const QDate& gameDate,
                                          const QString& homeAbbrev,
                                          const QString& awayAbbrev) {
  applyOptionalDate(gameDate, OptionalDateCommit::Placeholder);
  if (homeAbbrevEdit_) homeAbbrevEdit_->setText(homeAbbrev);
  if (awayAbbrevEdit_) awayAbbrevEdit_->setText(awayAbbrev);
  updateContinueButtonEnabled();
}

void GameSetupWindow::beginMetadataSuggestion(const QStringList& sourceVideoPaths) {
  abortMetadataSuggestion();
  if (!metadataSuggester_ || !XaiConfig::isConfigured()) {
    return;
  }
  connectMetadataSuggester();
  setSuggestionStatusKey("setup.ai_status_teams");
  metadataSuggester_->startSuggestionFromVideoPaths(sourceVideoPaths);
}

void GameSetupWindow::setInitialFocus() {
  if (homeNameEdit_) homeNameEdit_->setFocus();
}

void GameSetupWindow::applyUiStrings() const {
  if (titleLabel_) titleLabel_->setText(AppLocale::trUi("setup.title"));
  if (homeTeamLabel_) homeTeamLabel_->setText(AppLocale::trUi("setup.home_team"));
  if (awayTeamLabel_) awayTeamLabel_->setText(AppLocale::trUi("setup.away_team"));
  if (optionalLabel_) optionalLabel_->setText(AppLocale::trUi("setup.optional"));
  if (dateLabel_) dateLabel_->setText(AppLocale::trUi("setup.date"));
  if (homeNameEdit_) homeNameEdit_->setPlaceholderText(AppLocale::trUi("setup.placeholder_home_team"));
  if (awayNameEdit_) awayNameEdit_->setPlaceholderText(AppLocale::trUi("setup.placeholder_away_team"));
  if (homeAbbrevEdit_) homeAbbrevEdit_->setPlaceholderText(AppLocale::trUi("setup.placeholder_abbrev"));
  if (awayAbbrevEdit_) awayAbbrevEdit_->setPlaceholderText(AppLocale::trUi("setup.placeholder_abbrev"));
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
  if (suggestionStatusLabel_ && suggestionStatusKey_) {
    suggestionStatusLabel_->setText(AppLocale::trUi(suggestionStatusKey_));
  }
  updateContinueButtonEnabled();
}

void GameSetupWindow::onLanguageComboChanged(int index) {
  AppLocale::setLanguage(index == 1 ? AppLocale::Language::Spanish : AppLocale::Language::English);
  QSignalBlocker languageComboBlocker(languageCombo_);
  applyUiStrings();
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
  updateContinueButtonEnabled();
}

void GameSetupWindow::onAwayNameEditingFinished() {
  if (!awayAbbrevEdit_ || !awayNameEdit_) return;
  if (!awayAbbrevEdit_->text().trimmed().isEmpty()) return;
  const QString derived = deriveAbbreviationFromTeamName(awayNameEdit_->text());
  if (!derived.isEmpty()) {
    QSignalBlocker blocker(awayAbbrevEdit_);
    awayAbbrevEdit_->setText(derived);
  }
  updateContinueButtonEnabled();
}

void GameSetupWindow::onGameDateChanged(QDate) {
  optionalDateChosen_ = true;
  updateOptionalFieldAppearance();
}

void GameSetupWindow::applyOptionalDate(const QDate& date, OptionalDateCommit commit) {
  if (gameDateEdit_) {
    const QDate dateToShow = date.isValid() ? date : QDate::currentDate();
    const QSignalBlocker dateChangeBlocker(gameDateEdit_);
    gameDateEdit_->setDate(dateToShow);
  }
  optionalDateChosen_ = (commit == OptionalDateCommit::Chosen);
  updateOptionalFieldAppearance();
}

void GameSetupWindow::updateOptionalFieldAppearance() const {
  if (!gameDateEdit_ || !dateLabel_) return;
  const char* dateState = optionalDateChosen_ ? "active" : "stale";
  Style::setProp(gameDateEdit_, "optionalState", dateState);
  Style::setProp(dateLabel_, "optionalState", dateState);
}

void GameSetupWindow::buildUi() {
  auto outerLayout = makeQtPtr<QVBoxLayout>(this);
  outerLayout->setContentsMargins(24, 16, 24, 24);
  outerLayout->setSpacing(0);

  auto headerRow = makeQtPtr<QHBoxLayout>();
  headerRow->setContentsMargins(0, 0, 0, 0);
  auto languageCombo = makeQtPtr<QComboBox>(this);
  languageCombo->setObjectName(QStringLiteral("GameSetupLanguageCombo"));
  languageCombo->addItem(QString());
  languageCombo->addItem(QString());
  languageCombo->setMaximumWidth(140);
  languageCombo->setFocusPolicy(Qt::ClickFocus);
  Style::setVariant(languageCombo.get(), "compact");
  languageCombo_ = languageCombo.get();
  headerRow->addStretch(1);
  headerRow->addWidget(languageCombo.get(), 0, Qt::AlignTop);
  outerLayout->addLayout(headerRow.get());

  outerLayout->addStretch(1);

  auto contentContainer = makeQtPtr<QWidget>(this);
  contentContainer->setMinimumWidth(440);
  contentContainer->setMaximumWidth(520);
  auto layout = makeQtPtr<QVBoxLayout>(contentContainer.get());
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(20);

  auto titleLabel = makeQtPtr<QLabel>(contentContainer.get());
  titleLabel->setWordWrap(true);
  titleLabel->setAlignment(Qt::AlignCenter);
  Style::setRole(titleLabel.get(), "h1");
  titleLabel_ = titleLabel.get();
  layout->addWidget(titleLabel.get(), 0, Qt::AlignHCenter);

  auto suggestionStatusLabel = makeQtPtr<QLabel>(contentContainer.get());
  suggestionStatusLabel->setWordWrap(true);
  suggestionStatusLabel->setAlignment(Qt::AlignCenter);
  Style::setRole(suggestionStatusLabel.get(), "faint");
  suggestionStatusLabel->hide();
  suggestionStatusLabel_ = suggestionStatusLabel.get();
  layout->addWidget(suggestionStatusLabel.get(), 0, Qt::AlignHCenter);

  auto addTeamGroup = [&](QPointer<QLabel>& teamLabel, QPointer<QLineEdit>& nameEdit,
                          QPointer<QLineEdit>& abbrevEdit, QPointer<TeamColorPicker>& colorPicker,
                          bool homeSide) {
    auto group = makeQtPtr<QWidget>(contentContainer.get());
    auto groupLayout = makeQtPtr<QVBoxLayout>(group.get());
    groupLayout->setContentsMargins(0, 0, 0, 0);
    groupLayout->setSpacing(8);

    auto groupTeamLabel = makeQtPtr<QLabel>(group.get());
    Style::setRole(groupTeamLabel.get(), "h3");
    teamLabel = groupTeamLabel.get();
    groupLayout->addWidget(groupTeamLabel.get());

    auto fieldsRow = makeQtPtr<QHBoxLayout>();
    fieldsRow->setContentsMargins(0, 0, 0, 0);
    fieldsRow->setSpacing(8);

    auto groupNameEdit = makeQtPtr<QLineEdit>(group.get());
    auto groupAbbrevEdit = makeQtPtr<QLineEdit>(group.get());
    groupAbbrevEdit->setMaxLength(3);
    groupAbbrevEdit->setMaximumWidth(80);
    groupAbbrevEdit->setMinimumWidth(64);

    auto groupColorPicker = makeQtPtr<TeamColorPicker>(group.get());
    groupColorPicker->setFallbackPreviewColor(homeSide ? QColor(Qt::blue) : QColor(Qt::red));

    nameEdit = groupNameEdit.get();
    abbrevEdit = groupAbbrevEdit.get();
    colorPicker = groupColorPicker.get();

    fieldsRow->addWidget(groupNameEdit.get(), 1);
    fieldsRow->addWidget(groupAbbrevEdit.get(), 0);
    fieldsRow->addWidget(groupColorPicker.get(), 0, Qt::AlignVCenter);
    groupLayout->addLayout(fieldsRow.get());
    layout->addWidget(group.get());
  };

  addTeamGroup(homeTeamLabel_, homeNameEdit_, homeAbbrevEdit_, homeColorPicker_, true);
  addTeamGroup(awayTeamLabel_, awayNameEdit_, awayAbbrevEdit_, awayColorPicker_, false);

  auto optionalLabel = makeQtPtr<QLabel>(contentContainer.get());
  Style::setRole(optionalLabel.get(), "faint");
  optionalLabel_ = optionalLabel.get();
  layout->addWidget(optionalLabel.get());

  auto optionalBlock = makeQtPtr<QWidget>(contentContainer.get());
  optionalBlock->setObjectName(QStringLiteral("OptionalSetupFields"));
  auto optionalLayout = makeQtPtr<QVBoxLayout>(optionalBlock.get());
  optionalLayout->setContentsMargins(0, 0, 0, 0);
  optionalLayout->setSpacing(8);

  auto addOptionalRow = [&](QPointer<QLabel>& label, QWidget* field) {
    auto row = makeQtPtr<QHBoxLayout>();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);
    auto rowLabel = makeQtPtr<QLabel>(optionalBlock.get());
    rowLabel->setMinimumWidth(96);
    Style::setRole(rowLabel.get(), "faint");
    label = rowLabel.get();
    row->addWidget(rowLabel.get(), 0);
    row->addWidget(field, 1);
    optionalLayout->addLayout(row.get());
  };

  auto gameDateEdit = makeQtPtr<QDateEdit>(QDate::currentDate(), optionalBlock.get());
  gameDateEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
  gameDateEdit->setCalendarPopup(true);
  gameDateEdit->setMaximumWidth(180);
  gameDateEdit_ = gameDateEdit.get();
  addOptionalRow(dateLabel_, gameDateEdit.get());

  layout->addWidget(optionalBlock.get());

  auto buttonRow = makeQtPtr<QHBoxLayout>();
  buttonRow->setSpacing(12);
  auto backButton = makeQtPtr<QPushButton>(contentContainer.get());
  backButton->setCursor(Qt::PointingHandCursor);
  backButton->setAutoDefault(false);
  backButton->setDefault(false);
  Style::setVariant(backButton.get(), "ghost");
  Style::setSize(backButton.get(), "md");
  backButton_ = backButton.get();
  auto continueButton = makeQtPtr<QPushButton>(contentContainer.get());
  continueButton->setCursor(Qt::PointingHandCursor);
  continueButton->setAutoDefault(false);
  continueButton->setDefault(false);
  continueButton->setEnabled(false);
  Style::setVariant(continueButton.get(), "welcomeImport");
  Style::setSize(continueButton.get(), "lg");
  continueButton_ = continueButton.get();
  buttonRow->addStretch(1);
  buttonRow->addWidget(backButton.get(), 0);
  buttonRow->addWidget(continueButton.get(), 0);
  buttonRow->addStretch(1);
  layout->addLayout(buttonRow.get());

  outerLayout->addWidget(contentContainer.get(), 0, Qt::AlignCenter);
  outerLayout->addStretch(1);

  setTabOrder(homeNameEdit_, homeAbbrevEdit_);
  setTabOrder(homeAbbrevEdit_, homeColorPicker_);
  setTabOrder(homeColorPicker_, awayNameEdit_);
  setTabOrder(awayNameEdit_, awayAbbrevEdit_);
  setTabOrder(awayAbbrevEdit_, awayColorPicker_);
  setTabOrder(awayColorPicker_, gameDateEdit_);
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
  connect(homeNameEdit_, &QLineEdit::textChanged, this, [this] { updateContinueButtonEnabled(); });
  connect(awayNameEdit_, &QLineEdit::textChanged, this, [this] { updateContinueButtonEnabled(); });
  connect(homeAbbrevEdit_, &QLineEdit::textChanged, this, [this] { updateContinueButtonEnabled(); });
  connect(awayAbbrevEdit_, &QLineEdit::textChanged, this, [this] { updateContinueButtonEnabled(); });
  connect(homeColorPicker_, &TeamColorPicker::colorChanged, this, [this] { updateContinueButtonEnabled(); });
  connect(awayColorPicker_, &TeamColorPicker::colorChanged, this, [this] { updateContinueButtonEnabled(); });
  connect(gameDateEdit_, &QDateEdit::dateChanged, this, &GameSetupWindow::onGameDateChanged);
}

void GameSetupWindow::connectMetadataSuggester() {
  disconnectMetadataSuggester();
  if (!metadataSuggester_) return;
  GameMetadataSuggester* suggester = metadataSuggester_.get();
  metadataSuggesterConnections_.append(
      connect(suggester, &GameMetadataSuggester::nameDateSuggested, this,
              &GameSetupWindow::onNameDateSuggested));
  metadataSuggesterConnections_.append(
      connect(suggester, &GameMetadataSuggester::colorDetectionStarted, this,
              &GameSetupWindow::onColorDetectionStarted));
  metadataSuggesterConnections_.append(
      connect(suggester, &GameMetadataSuggester::colorsSuggested, this,
              &GameSetupWindow::onColorsSuggested));
  metadataSuggesterConnections_.append(
      connect(suggester, &GameMetadataSuggester::finished, this,
              &GameSetupWindow::onMetadataSuggestionFinished));
  suggestionSignalsArmed_ = true;
}

void GameSetupWindow::disconnectMetadataSuggester() {
  suggestionSignalsArmed_ = false;
  for (const QMetaObject::Connection& connection : metadataSuggesterConnections_) {
    QObject::disconnect(connection);
  }
  metadataSuggesterConnections_.clear();
  if (metadataSuggester_) {
    metadataSuggester_->disconnect(this);
  }
}

void GameSetupWindow::abortMetadataSuggestion() {
  disconnectMetadataSuggester();
  if (metadataSuggester_) {
    const QSignalBlocker suggesterSignalBlocker(metadataSuggester_.get());
    metadataSuggester_->abort();
  }
  setSuggestionStatusKey(nullptr);
}

void GameSetupWindow::discardMetadataSuggester() {
  disconnectMetadataSuggester();
  if (!metadataSuggester_) return;
  GameMetadataSuggester* dyingSuggester = metadataSuggester_.release();
  dyingSuggester->disconnect();
  dyingSuggester->blockSignals(true);
  dyingSuggester->abort();
  dyingSuggester->deleteLater();
  setSuggestionStatusKey(nullptr);
}

bool GameSetupWindow::suggestionSignalsArmed() const {
  return suggestionSignalsArmed_ && metadataSuggester_
      && metadataSuggester_->isRunning();
}

void GameSetupWindow::setSuggestionStatusKey(const char* key) {
  suggestionStatusKey_ = key;
  if (!suggestionStatusLabel_) {
    return;
  }
  if (!suggestionStatusKey_) {
    suggestionStatusLabel_->clear();
    suggestionStatusLabel_->hide();
    return;
  }
  suggestionStatusLabel_->setText(AppLocale::trUi(suggestionStatusKey_));
  suggestionStatusLabel_->show();
}

void GameSetupWindow::onNameDateSuggested(const QString& homeTeamName,
                                         const QString& awayTeamName,
                                         const QDate& gameDate) {
  if (!suggestionSignalsArmed()) return;
  if (homeNameEdit_ && homeNameEdit_->text().trimmed().isEmpty() && !homeTeamName.trimmed().isEmpty()) {
    homeNameEdit_->setText(homeTeamName.trimmed());
    onHomeNameEditingFinished();
  }
  if (awayNameEdit_ && awayNameEdit_->text().trimmed().isEmpty() && !awayTeamName.trimmed().isEmpty()) {
    awayNameEdit_->setText(awayTeamName.trimmed());
    onAwayNameEditingFinished();
  }
  if (gameDate.isValid() && !optionalDateChosen_) {
    applyOptionalDate(gameDate, OptionalDateCommit::Chosen);
  }
  updateContinueButtonEnabled();
}

void GameSetupWindow::onColorDetectionStarted() {
  if (!suggestionSignalsArmed()) return;
  setSuggestionStatusKey("setup.ai_status_colors");
}

void GameSetupWindow::onColorsSuggested(const QString& homeColorHex, const QString& awayColorHex) {
  if (!suggestionSignalsArmed()) return;
  if (homeColorPicker_ && homeColorPicker_->color().trimmed().isEmpty() && !homeColorHex.trimmed().isEmpty()) {
    homeColorPicker_->setColor(homeColorHex);
  }
  if (awayColorPicker_ && awayColorPicker_->color().trimmed().isEmpty() && !awayColorHex.trimmed().isEmpty()) {
    awayColorPicker_->setColor(awayColorHex);
  }
  updateContinueButtonEnabled();
}

void GameSetupWindow::onMetadataSuggestionFinished() {
  if (!suggestionSignalsArmed_) return;
  setSuggestionStatusKey(nullptr);
}

void GameSetupWindow::updateContinueButtonEnabled() const {
  if (!continueButton_) return;
  const bool canContinue = hasRequiredSetupFields(collectSetupFormValues());
  continueButton_->setEnabled(canContinue);
  continueButton_->setCursor(canContinue ? Qt::PointingHandCursor : Qt::ArrowCursor);
  continueButton_->setToolTip(canContinue ? QString() : AppLocale::trUi("setup.continue_disabled_hint"));
}

GameSetupWindow::SetupFormValues GameSetupWindow::collectSetupFormValues() const {
  SetupFormValues values;
  values.homeName = homeNameEdit_ ? homeNameEdit_->text().trimmed() : QString();
  values.awayName = awayNameEdit_ ? awayNameEdit_->text().trimmed() : QString();
  values.homeColor = homeColorPicker_ ? homeColorPicker_->color().trimmed() : QString();
  values.awayColor = awayColorPicker_ ? awayColorPicker_->color().trimmed() : QString();
  values.homeAbbrev = homeAbbrevEdit_ ? homeAbbrevEdit_->text().trimmed().toUpper() : QString();
  values.awayAbbrev = awayAbbrevEdit_ ? awayAbbrevEdit_->text().trimmed().toUpper() : QString();
  if (values.homeAbbrev.isEmpty()) {
    values.homeAbbrev = deriveAbbreviationFromTeamName(values.homeName);
  }
  if (values.awayAbbrev.isEmpty()) {
    values.awayAbbrev = deriveAbbreviationFromTeamName(values.awayName);
  }
  values.gameDate = gameDateEdit_ ? gameDateEdit_->date() : QDate();
  return values;
}

bool GameSetupWindow::hasRequiredSetupFields(const SetupFormValues& values) const {
  return !videoPath_.trimmed().isEmpty()
      && !values.homeName.isEmpty()
      && !values.awayName.isEmpty()
      && !values.homeAbbrev.isEmpty()
      && !values.awayAbbrev.isEmpty()
      && !values.homeColor.isEmpty()
      && !values.awayColor.isEmpty();
}

void GameSetupWindow::onContinue() {
  const SetupFormValues values = collectSetupFormValues();
  if (!hasRequiredSetupFields(values)) {
    updateContinueButtonEnabled();
    return;
  }

  abortMetadataSuggestion();
  emit gameSetupConfirmed(videoPath_, values.homeName, values.awayName, values.homeColor,
                          values.awayColor, QString(), values.gameDate, values.homeAbbrev,
                          values.awayAbbrev);
}

void GameSetupWindow::onBack() {
  abortMetadataSuggestion();
  emit cancelled();
}
