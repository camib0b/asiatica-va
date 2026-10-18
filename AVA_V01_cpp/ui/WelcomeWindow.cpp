#include "WelcomeWindow.h"
#include "QtPtr.h"
#include "../style/Motion.h"
#include "../style/StyleProps.h"
#include "../i18n/AppLocale.h"
#include "../license/LicenseManager.h"
#include "../license/LicenseTypes.h"

#include <QLabel>
#include <QPushButton>
#include <QWidget>
#include <QVBoxLayout>
#include <QAction>
#include <QDateTime>
#include <QTimeZone>
#include <QSizePolicy>
#include <QEvent>
#include <QShowEvent>

namespace {

QWidget* mountEntrance(QWidget* parent, QWidget* child, QVBoxLayout* column) {
  auto frame = makeQtPtr<QWidget>(parent);
  auto layout = makeQtPtr<QVBoxLayout>(frame.get());
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(child, 0, Qt::AlignHCenter);
  column->addWidget(frame.get(), 0, Qt::AlignHCenter);
  return frame.release();
}

}  // namespace


WelcomeWindow::WelcomeWindow(QWidget* parent)
    : QWidget(parent),
      titleLabel_(nullptr),
      importButton_(nullptr),
      licenseStatusLabel_(nullptr),
      enterLicenseButton_(nullptr) {
    setObjectName("AppRoot");
    setAttribute(Qt::WA_StyledBackground, true);
    buildUi();
    wireSignals();
    buildKeyboardShortcuts();
    applyUiStrings();
    setMinimumSize(320, 250);
}

void WelcomeWindow::applyUiStrings() {
    if (titleLabel_) titleLabel_->setText(QStringLiteral("ava"));
    if (importButton_) {
        importButton_->setText(AppLocale::trUi("welcome.import"));
        syncImportButtonMinimumWidth();
    }
    if (enterLicenseButton_) {
        enterLicenseButton_->setText(AppLocale::trUi("license.enter_key"));
    }
    if (licenseStatusLabel_) {
        const LicenseUiStatus status = LicenseManager::instance().uiStatus();
        if (status.entitled && status.kind == QLatin1String("trial")) {
            licenseStatusLabel_->setText(AppLocale::trUi("license.status.trial").arg(status.daysRemaining));
        } else if (status.entitled && status.kind == QLatin1String("paid") && status.expiresAt > 0) {
            const QString dateText =
                QDateTime::fromSecsSinceEpoch(status.expiresAt, QTimeZone::utc()).date().toString(Qt::ISODate);
            licenseStatusLabel_->setText(AppLocale::trUi("license.status.paid").arg(dateText));
        } else {
            licenseStatusLabel_->clear();
        }
    }
}

void WelcomeWindow::buildUi() {
    auto outerLayout = makeQtPtr<QVBoxLayout>(this);
    outerLayout->setContentsMargins(24, 24, 24, 24);
    outerLayout->addStretch(1);

    auto contentContainer = makeQtPtr<QWidget>(this);
    auto layout = makeQtPtr<QVBoxLayout>(contentContainer.get());
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto titleLabel = makeQtPtr<QLabel>(contentContainer.get());
    titleLabel->setWordWrap(false);
    titleLabel->setAlignment(Qt::AlignCenter);
    Style::setRole(titleLabel.get(), "h1");
    titleLabel_ = titleLabel.get();

    auto importButton = makeQtPtr<QPushButton>(contentContainer.get());
    importButton->setCursor(Qt::PointingHandCursor);
    Style::setVariant(importButton.get(), "welcomeImport");
    Style::setSize(importButton.get(), "lg");
    importButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    importButton->setFocusPolicy(Qt::TabFocus); // Allow keyboard focus but don't auto-focus on window open
    importButton_ = importButton.get();

    auto licenseStatusLabel = makeQtPtr<QLabel>(contentContainer.get());
    licenseStatusLabel->setAlignment(Qt::AlignCenter);
    Style::setRole(licenseStatusLabel.get(), "muted");
    licenseStatusLabel_ = licenseStatusLabel.get();

    auto enterLicenseButton = makeQtPtr<QPushButton>(contentContainer.get());
    enterLicenseButton->setCursor(Qt::PointingHandCursor);
    enterLicenseButton->setFlat(true);
    enterLicenseButton->setFocusPolicy(Qt::TabFocus);
    enterLicenseButton_ = enterLicenseButton.get();

    titleEntrance_ = mountEntrance(contentContainer.get(), titleLabel.get(), layout.get());
    importEntrance_ = mountEntrance(contentContainer.get(), importButton.get(), layout.get());

    auto licenseRow = makeQtPtr<QWidget>(contentContainer.get());
    auto licenseLayout = makeQtPtr<QVBoxLayout>(licenseRow.get());
    licenseLayout->setContentsMargins(0, 0, 0, 0);
    licenseLayout->setSpacing(12);
    licenseLayout->addWidget(licenseStatusLabel.get(), 0, Qt::AlignHCenter);
    licenseLayout->addWidget(enterLicenseButton.get(), 0, Qt::AlignHCenter);
    layout->addWidget(licenseRow.get(), 0, Qt::AlignHCenter);
    licenseEntrance_ = licenseRow.release();

    outerLayout->addWidget(contentContainer.get(), 0, Qt::AlignCenter);
    outerLayout->addStretch(1);
}

void WelcomeWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (entrancePlayed_) return;
    entrancePlayed_ = true;
    playEntrance();
}

void WelcomeWindow::playEntrance() {
    // The import button is a child of its entrance frame, so it stays enabled
    // and clickable while the frame is still transparent.
    Motion::fadeIn(titleEntrance_, Motion::kEnter, 8, 0);
    Motion::fadeIn(importEntrance_, Motion::kEnter, 8, Motion::kStagger);
    Motion::fadeIn(licenseEntrance_, Motion::kEnter, 8, Motion::kStagger * 2);
}

void WelcomeWindow::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (!event) return;
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange) {
        syncImportButtonMinimumWidth();
    }
}

void WelcomeWindow::syncImportButtonMinimumWidth() {
    if (!importButton_) return;
    importButton_->setMinimumWidth(Style::pushButtonMinimumWidth(importButton_));
}

void WelcomeWindow::wireSignals() {
    connect(importButton_, &QPushButton::clicked, this, &WelcomeWindow::videoImportRequested);
    connect(enterLicenseButton_, &QPushButton::clicked, this, &WelcomeWindow::enterLicenseRequested);
}

void WelcomeWindow::buildKeyboardShortcuts() {
    auto importShortcut = makeQtPtr<QAction>(this);
    importShortcut->setShortcut(QKeySequence(Qt::Key_S));
    // Only while this stacked page has focus; Window/Application would still fire on WorkWindow.
    importShortcut->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(importShortcut.get(), &QAction::triggered, this, [this]() {
        if (importButton_ && importButton_->isEnabled()) {
            importButton_->click();
        }
    });
    addAction(importShortcut.get());
}
