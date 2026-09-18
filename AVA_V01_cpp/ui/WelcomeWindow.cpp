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
#include <QDateTime>
#include <QTimeZone>
#include <QSizePolicy>
#include <QStyle>
#include <QStyleOptionButton>
#include <QEvent>
#include <QObject>

#include <memory>
#include <utility>


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

namespace {

struct QtParentDeleter {
    void operator()(QObject* object) const noexcept {
        if (object != nullptr && object->parent() == nullptr) {
            delete object;
        }
    }
};

template<typename Object, typename... Args>
std::unique_ptr<Object, QtParentDeleter> makeQtPtr(Args&&... args) {
    return std::unique_ptr<Object, QtParentDeleter>(
        std::make_unique<Object>(std::forward<Args>(args)...).release());
}

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

    layout->addWidget(titleLabel.get(), 0, Qt::AlignHCenter);
    layout->addWidget(importButton.get(), 0, Qt::AlignHCenter);
    layout->addWidget(licenseStatusLabel.get(), 0, Qt::AlignHCenter);
    layout->addWidget(enterLicenseButton.get(), 0, Qt::AlignHCenter);

    outerLayout->addWidget(contentContainer.get(), 0, Qt::AlignCenter);
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
