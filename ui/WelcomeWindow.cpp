#include "WelcomeWindow.h"
#include "../style/StyleProps.h"

#include <QLabel>
#include <QPushButton>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>

WelcomeWindow::WelcomeWindow(QWidget* parent) : QWidget(parent) {
    setObjectName("AppRoot");
    setAttribute(Qt::WA_StyledBackground, true);
    buildUi();
    wireSignals();
    setMinimumSize(250, 250);
}

void WelcomeWindow::buildUi() {
    speedLabel_ = new QLabel(this);
    Style::setRole(speedLabel_, "muted");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);

    // header:
    headerLabel_ = new QLabel("this is ava", this);
    headerLabel_->setWordWrap(true);
    Style::setRole(headerLabel_, "hero");
    

    // subtitle:
    subtitleLabel_ = new QLabel("Import a video file to get started", this);
    subtitleLabel_->setWordWrap(true);
    Style::setRole(subtitleLabel_, "subhero");    

    // import button:
    importButton_ = new QPushButton("&Select video file", this);
    importButton_->setCursor(Qt::PointingHandCursor);
    Style::setVariant(importButton_, "primary");
    Style::setSize(importButton_, "lg");
    importButton_->setMaximumWidth(335);


    // Add widgets vertically, aligned to top-left
    layout->addWidget(headerLabel_);
    layout->addWidget(subtitleLabel_);
    layout->addWidget(importButton_);
    layout->addStretch(1);
}

void WelcomeWindow::wireSignals() {
    connect(importButton_, &QPushButton::clicked, this, &WelcomeWindow::videoImportRequested);
}
