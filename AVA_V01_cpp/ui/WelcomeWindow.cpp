#include "WelcomeWindow.h"
#include "../style/StyleProps.h"
#include "../i18n/AppLocale.h"
#include "../license/LicenseManager.h"
#include "../license/LicenseTypes.h"

#include <QLabel>
#include <QPushButton>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QAction>
#include <QFontMetrics>
#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QSizePolicy>


WelcomeWindow::WelcomeWindow(QWidget* parent) : QWidget(parent) {
    setObjectName("AppRoot");
    setAttribute(Qt::WA_StyledBackground, true);
    buildUi();
    wireSignals();
    buildKeyboardShortcuts();
    applyUiStrings();
    setMinimumSize(320, 250);
}

namespace {

int welcomeImportButtonMinimumWidth(const QPushButton* button) {
    if (!button) return 0;

    const QFontMetrics metrics(button->font());
    const int textWidth =
        metrics.boundingRect(0, 0, 0, 0, Qt::TextShowMnemonic, button->text()).width();
    // Stylesheet padding is not included in QPushButton::sizeHint().
    constexpr int horizontalPaddingPx = 48; // lg size: 24px left + 24px right
    constexpr int focusBorderSlackPx = 4;   // 2px focus ring vs 1px normal border
    return textWidth + horizontalPaddingPx + focusBorderSlackPx;
}

} // namespace

void WelcomeWindow::applyUiStrings() {
    if (titleLabel_) titleLabel_->setText(QStringLiteral("ava"));
    if (importButton_) {
        importButton_->setText(AppLocale::trUi("welcome.import"));
        importButton_->setMinimumWidth(welcomeImportButtonMinimumWidth(importButton_));
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
                QDateTime::fromSecsSinceEpoch(status.expiresAt, Qt::UTC).date().toString(Qt::ISODate);
            licenseStatusLabel_->setText(AppLocale::trUi("license.status.paid").arg(dateText));
        } else {
            licenseStatusLabel_->clear();
        }
    }
}

void WelcomeWindow::buildUi() {
    speedLabel_ = new QLabel(this);
    Style::setRole(speedLabel_, "muted");

    // Outer layout for centering
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(24, 24, 24, 24);
    outerLayout->addStretch(1);

    // Inner container for content
    auto* contentContainer = new QWidget(this);
    auto* layout = new QVBoxLayout(contentContainer);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    titleLabel_ = new QLabel(contentContainer);
    titleLabel_->setWordWrap(false);
    titleLabel_->setAlignment(Qt::AlignCenter);
    Style::setRole(titleLabel_, "h1");

    // import button:
    importButton_ = new QPushButton(contentContainer);
    importButton_->setCursor(Qt::PointingHandCursor);
    Style::setVariant(importButton_, "welcomeImport");
    Style::setSize(importButton_, "lg");
    importButton_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    importButton_->setFocusPolicy(Qt::TabFocus); // Allow keyboard focus but don't auto-focus on window open

    licenseStatusLabel_ = new QLabel(contentContainer);
    licenseStatusLabel_->setAlignment(Qt::AlignCenter);
    Style::setRole(licenseStatusLabel_, "muted");

    enterLicenseButton_ = new QPushButton(contentContainer);
    enterLicenseButton_->setCursor(Qt::PointingHandCursor);
    enterLicenseButton_->setFlat(true);
    enterLicenseButton_->setFocusPolicy(Qt::TabFocus);

    // Add widgets vertically, centered
    layout->addWidget(titleLabel_, 0, Qt::AlignHCenter);
    layout->addWidget(importButton_, 0, Qt::AlignHCenter);
    layout->addWidget(licenseStatusLabel_, 0, Qt::AlignHCenter);
    layout->addWidget(enterLicenseButton_, 0, Qt::AlignHCenter);

    // Center content container in outer layout
    outerLayout->addWidget(contentContainer, 0, Qt::AlignCenter);
    outerLayout->addStretch(1);
}

void WelcomeWindow::wireSignals() {
    connect(importButton_, &QPushButton::clicked, this, &WelcomeWindow::videoImportRequested);
    connect(enterLicenseButton_, &QPushButton::clicked, this, &WelcomeWindow::enterLicenseRequested);
}

void WelcomeWindow::buildKeyboardShortcuts() {
    Q_ASSERT(QApplication::instance() != nullptr);

    auto makeAction = [this](const QKeySequence& seq) -> QAction* {
        auto* act = new QAction(this);
        act->setShortcut(seq);
        act->setShortcutContext(Qt::ApplicationShortcut);
        connect(act, &QAction::triggered, this, [this]() {
            if (importButton_ && importButton_->isEnabled()) {
                importButton_->click();
            }
        });
        this->addAction(act);
        return act;
    };

    // Keyboard shortcuts for import button: 's', spacebar, enter
    makeAction(QKeySequence(Qt::Key_S));
    makeAction(QKeySequence(Qt::Key_Space));
    makeAction(QKeySequence(Qt::Key_Return));
    makeAction(QKeySequence(Qt::Key_Enter));
}
