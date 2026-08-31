#include "LicenseLockOverlay.h"

#include "../i18n/AppLocale.h"
#include "../i18n/LocaleNotifier.h"
#include "../license/LicenseManager.h"
#include "../license/LicenseTypes.h"
#include "../style/StyleProps.h"

#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

LicenseLockOverlay::LicenseLockOverlay(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("AppRoot"));
  setAttribute(Qt::WA_StyledBackground, true);
  buildUi();
  applyUiStrings();
  connect(&LicenseManager::instance(), &LicenseManager::entitlementChanged, this,
          &LicenseLockOverlay::onLicenseStateChanged);
  connect(&LicenseManager::instance(), &LicenseManager::userMessageChanged, this,
          &LicenseLockOverlay::onLicenseStateChanged);
  connect(&LicenseManager::instance(), &LicenseManager::busyChanged, this,
          &LicenseLockOverlay::onLicenseStateChanged);
  connect(&LocaleNotifier::instance(), &LocaleNotifier::languageChanged, this,
          &LicenseLockOverlay::applyUiStrings);
}

void LicenseLockOverlay::setCloseAllowed(bool allowed) {
  closeAllowed_ = allowed;
  if (closeButton_) closeButton_->setVisible(allowed);
}

void LicenseLockOverlay::refreshCopy() {
  applyUiStrings();
}

void LicenseLockOverlay::buildUi() {
  auto* outerLayout = new QVBoxLayout(this);
  outerLayout->setContentsMargins(24, 24, 24, 24);
  outerLayout->addStretch(1);

  auto* content = new QWidget(this);
  content->setMaximumWidth(560);
  auto* layout = new QVBoxLayout(content);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);

  languageCombo_ = new QComboBox(content);
  languageCombo_->addItem(QString(), 0);
  languageCombo_->addItem(QString(), 1);
  languageCombo_->setMaximumWidth(180);
  connect(languageCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &LicenseLockOverlay::onLanguageChanged);
  layout->addWidget(languageCombo_, 0, Qt::AlignLeft);

  titleLabel_ = new QLabel(content);
  titleLabel_->setWordWrap(true);
  titleLabel_->setAlignment(Qt::AlignLeft);
  Style::setRole(titleLabel_, "h2");
  layout->addWidget(titleLabel_);

  bodyLabel_ = new QLabel(content);
  bodyLabel_->setWordWrap(true);
  Style::setRole(bodyLabel_, "subhero");
  layout->addWidget(bodyLabel_);

  emailLabel_ = new QLabel(content);
  Style::setRole(emailLabel_, "muted");
  layout->addWidget(emailLabel_);
  emailEdit_ = new QLineEdit(content);
  emailEdit_->setMinimumHeight(32);
  layout->addWidget(emailEdit_);

  keyLabel_ = new QLabel(content);
  Style::setRole(keyLabel_, "muted");
  layout->addWidget(keyLabel_);
  keyEdit_ = new QPlainTextEdit(content);
  keyEdit_->setMinimumHeight(96);
  keyEdit_->setMaximumHeight(140);
  layout->addWidget(keyEdit_);

  auto* buttonRow = new QHBoxLayout();
  buttonRow->setSpacing(12);
  activateButton_ = new QPushButton(content);
  activateButton_->setCursor(Qt::PointingHandCursor);
  activateButton_->setDefault(true);
  Style::setSize(activateButton_, "lg");
  connect(activateButton_, &QPushButton::clicked, this, &LicenseLockOverlay::onActivateClicked);
  buttonRow->addWidget(activateButton_);

  loadFileButton_ = new QPushButton(content);
  loadFileButton_->setCursor(Qt::PointingHandCursor);
  loadFileButton_->setAutoDefault(false);
  connect(loadFileButton_, &QPushButton::clicked, this, &LicenseLockOverlay::onLoadFileClicked);
  buttonRow->addWidget(loadFileButton_);

  closeButton_ = new QPushButton(content);
  closeButton_->setCursor(Qt::PointingHandCursor);
  closeButton_->setAutoDefault(false);
  closeButton_->setVisible(false);
  connect(closeButton_, &QPushButton::clicked, this, &LicenseLockOverlay::closeRequested);
  buttonRow->addWidget(closeButton_);
  buttonRow->addStretch(1);
  layout->addLayout(buttonRow);

  messageLabel_ = new QLabel(content);
  messageLabel_->setWordWrap(true);
  Style::setRole(messageLabel_, "muted");
  layout->addWidget(messageLabel_);

  contactLabel_ = new QLabel(content);
  contactLabel_->setWordWrap(true);
  contactLabel_->setOpenExternalLinks(true);
  Style::setRole(contactLabel_, "muted");
  layout->addWidget(contactLabel_);

  outerLayout->addWidget(content, 0, Qt::AlignHCenter);
  outerLayout->addStretch(1);
}

void LicenseLockOverlay::applyUiStrings() {
  if (languageCombo_) {
    QSignalBlocker blocker(languageCombo_);
    languageCombo_->setItemText(0, AppLocale::trUi("setup.lang_en"));
    languageCombo_->setItemText(1, AppLocale::trUi("setup.lang_es"));
    languageCombo_->setCurrentIndex(
        AppLocale::currentLanguage() == AppLocale::Language::Spanish ? 1 : 0);
    languageCombo_->setToolTip(AppLocale::trUi("setup.lang_label"));
    languageCombo_->setAccessibleName(AppLocale::trUi("setup.lang_label"));
  }

  const LicenseUiStatus status = LicenseManager::instance().uiStatus();
  const char* titleKey = "license.lock.title_trial";
  const char* bodyKey = "license.lock.body_trial";
  switch (status.lockReason) {
    case LicenseLockReason::GraceExhausted:
      titleKey = "license.lock.title_grace";
      bodyKey = "license.lock.body_grace";
      break;
    case LicenseLockReason::PaidExpired:
      titleKey = "license.lock.title_expired";
      bodyKey = "license.lock.body_expired";
      break;
    case LicenseLockReason::OtherDevice:
      titleKey = "license.lock.title_other_device";
      bodyKey = "license.lock.body_other_device";
      break;
    case LicenseLockReason::None:
      titleKey = "license.lock.title_enter";
      bodyKey = "license.lock.body_enter";
      break;
    case LicenseLockReason::Invalid:
    case LicenseLockReason::TrialEnded:
      break;
  }
  if (titleLabel_) titleLabel_->setText(AppLocale::trUi(titleKey));
  if (bodyLabel_) bodyLabel_->setText(AppLocale::trUi(bodyKey));
  if (emailLabel_) emailLabel_->setText(AppLocale::trUi("license.lock.email"));
  if (keyLabel_) keyLabel_->setText(AppLocale::trUi("license.lock.key"));
  if (emailEdit_) emailEdit_->setPlaceholderText(AppLocale::trUi("license.lock.email_placeholder"));
  if (keyEdit_) keyEdit_->setPlaceholderText(AppLocale::trUi("license.lock.key_placeholder"));
  if (activateButton_) activateButton_->setText(AppLocale::trUi("license.lock.activate"));
  if (loadFileButton_) loadFileButton_->setText(AppLocale::trUi("license.lock.load_file"));
  if (closeButton_) closeButton_->setText(AppLocale::trUi("license.lock.close"));
  if (contactLabel_) {
    contactLabel_->setText(QStringLiteral("<a href=\"https://camilaescudero.cl\">%1</a>")
                               .arg(AppLocale::trUi("license.lock.contact").toHtmlEscaped()));
  }

    if (messageLabel_) {
    if (LicenseManager::instance().isBusy()) {
      messageLabel_->setText(AppLocale::trUi("license.lock.busy"));
    } else {
      const QString messageKey = LicenseManager::instance().lastUserMessage();
      if (messageKey.isEmpty()) {
        messageLabel_->clear();
      } else {
        const QByteArray keyBytes = messageKey.toUtf8();
        messageLabel_->setText(AppLocale::trUi(keyBytes.constData()));
      }
    }
  }

  if (activateButton_) {
    activateButton_->setEnabled(!LicenseManager::instance().isBusy());
  }
}

void LicenseLockOverlay::onActivateClicked() {
  const QString email = emailEdit_ ? emailEdit_->text() : QString();
  const QString key = keyEdit_ ? keyEdit_->toPlainText() : QString();
  LicenseManager::instance().activateLicense(email, key);
}

void LicenseLockOverlay::onLoadFileClicked() {
  const QString filePath = QFileDialog::getOpenFileName(
      this, AppLocale::trUi("license.lock.load_file"), QString(),
      AppLocale::trUi("license.file_filter"));
  if (filePath.isEmpty()) return;
  LicenseManager::instance().loadLicenseFile(filePath);
}

void LicenseLockOverlay::onLanguageChanged(int index) {
  AppLocale::setLanguage(index == 1 ? AppLocale::Language::Spanish : AppLocale::Language::English);
}

void LicenseLockOverlay::onLicenseStateChanged() {
  applyUiStrings();
}
