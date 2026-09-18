#include "WelcomeWindow.h"
#include "../style/StyleProps.h"
#include "../i18n/AppLocale.h"
#include "../license/LicenseManager.h"
#include "../license/LicenseTypes.h"

#include <QLabel>
#include <QPushButton>
#include <QWidget>
#include <QVBoxLayout>
#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QTimeZone>
#include <QSizePolicy>
#include <QStyle>
#include <QStyleOptionButton>
#include <QEvent>


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

int styledPushButtonMinimumWidth(const QPushButton* button) {
    if (!button) return 0;

    button->ensurePolished();

    auto widthForFocus = [button](bool focused) {
        QStyleOptionButton option;
        option.initFrom(button);
        if (focused) {
            option.state |= QStyle::State_HasFocus;
        } else {
            option.state &= ~QStyle::State_HasFocus;
        }
        option.text = button->text();
        option.icon = button->icon();
        option.iconSize = button->iconSize();

        const QSize textSize = button->fontMetrics().size(Qt::TextShowMnemonic, button->text());
        return button->style()->sizeFromContents(QStyle::CT_PushButton, &option, textSize, button).width();
    };

    // :focus rules can thicken the border or change padding; size for both states.
    return qMax(widthForFocus(false), widthForFocus(true));
}

} // namespace

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
                QDateTime::fromSecsSinceEpoch(status.expiresAt, QTimeZone::UTC).date().toString(Qt::ISODate);
            licenseStatusLabel_->setText(AppLocale::trUi("license.status.paid").arg(dateText));
        } else {
            licenseStatusLabel_->clear();
        }
    }
}

void WelcomeWindow::buildUi() {
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(24, 24, 24, 24);
    outerLayout->addStretch(1);

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

void WelcomeWindow::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (!event) return;
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange) {
        syncImportButtonMinimumWidth();
    }
}

void WelcomeWindow::syncImportButtonMinimumWidth() {
    if (!importButton_) return;
    importButton_->setMinimumWidth(styledPushButtonMinimumWidth(importButton_));
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
