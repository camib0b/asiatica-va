#include "WelcomeWindow.h"
#include "../style/StyleProps.h"
#include "../i18n/AppLocale.h"

#include <QLabel>
#include <QPushButton>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QAction>
#include <QKeySequence>
#include <QApplication>
#include <QDebug>


WelcomeWindow::WelcomeWindow(QWidget* parent) : QWidget(parent) {
    setObjectName("AppRoot");
    setAttribute(Qt::WA_StyledBackground, true);
    buildUi();
    wireSignals();
    buildKeyboardShortcuts();
    applyUiStrings();
    setMinimumSize(250, 250);
}

void WelcomeWindow::applyUiStrings() {
    if (titleLabel_) titleLabel_->setText(QStringLiteral("ava"));
    if (importButton_) importButton_->setText(AppLocale::trUi("welcome.import"));
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
    importButton_->setMaximumWidth(400);
    importButton_->setFocusPolicy(Qt::TabFocus); // Allow keyboard focus but don't auto-focus on window open

    // Add widgets vertically, centered
    layout->addWidget(titleLabel_, 0, Qt::AlignHCenter);
    layout->addWidget(importButton_, 0, Qt::AlignHCenter);

    // Center content container in outer layout
    outerLayout->addWidget(contentContainer, 0, Qt::AlignCenter);
    outerLayout->addStretch(1);
}

void WelcomeWindow::wireSignals() {
    connect(importButton_, &QPushButton::clicked, this, &WelcomeWindow::videoImportRequested);
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
