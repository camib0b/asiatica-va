#include "WelcomeWindow.h"
#include "QtPtr.h"
#include "../style/Motion.h"
#include "../style/StyleProps.h"
#include "../i18n/AppLocale.h"

#include <QLabel>
#include <QPushButton>
#include <QWidget>
#include <QVBoxLayout>
#include <QAction>
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
      importButton_(nullptr) {
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

    titleEntrance_ = mountEntrance(contentContainer.get(), titleLabel.get(), layout.get());
    importEntrance_ = mountEntrance(contentContainer.get(), importButton.get(), layout.get());

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
