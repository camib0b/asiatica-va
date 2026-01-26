#include "MainWindow.h"

#include <QFileDialog>
#include <QApplication>
#include <QStackedWidget>
#include <QWidget>

#include "WelcomeWindow.h"
#include "WorkWindow.h"
#include "StatsWindow.h"
#include "../state/TagSession.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) { // ctor-init
    setWindowTitle("AVA");
    resize(1300, 800);
    
    // Create a stacked widget to hold both windows
    auto* stack = new QStackedWidget(this);
    stack->setObjectName("AppRoot");
    stack->setAttribute(Qt::WA_StyledBackground, true);
    setCentralWidget(stack);
    
    // Create windows
    welcomeWindow_ = new WelcomeWindow(this);
    workWindow_ = new WorkWindow(this);
    tagSession_ = new TagSession(this);
    statsWindow_ = new StatsWindow(this);
    statsWindow_->hide();

    if (workWindow_) workWindow_->setTagSession(tagSession_);
    if (statsWindow_) statsWindow_->setTagSession(tagSession_);
    
    stack->addWidget(welcomeWindow_);
    stack->addWidget(workWindow_);
    
    // Connect signals
    connect(welcomeWindow_, &WelcomeWindow::videoImportRequested, this, &MainWindow::onVideoImportRequested);
    connect(workWindow_, &WorkWindow::videoClosed, this, &MainWindow::onVideoClosed);
    
    // Show welcome window initially
    stack->setCurrentWidget(welcomeWindow_);

    // State reset on app exit (start of state management)
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
        if (tagSession_) tagSession_->clear();
    });
}

void MainWindow::showWelcomeWindow() {
    if (auto* stack = qobject_cast<QStackedWidget*>(centralWidget())) {
        stack->setCurrentWidget(welcomeWindow_);
    }
}

void MainWindow::showWorkWindow(const QString& filePath) {
    if (workWindow_) workWindow_->loadVideoFromFile(filePath);
    if (auto* stack = qobject_cast<QStackedWidget*>(centralWidget())) {
        stack->setCurrentWidget(workWindow_);
    }

    if (statsWindow_) {
        // Show stats window without stealing focus
        statsWindow_->show();

        // Place next to main window for now
        const QRect g = this->geometry();
        statsWindow_->resize(420, g.height());
        statsWindow_->move(g.topRight() + QPoint(16, 0));

        statsWindow_->lower();
        this->raise();
        this->activateWindow();
    }
}


void MainWindow::onVideoImportRequested() {
    const QString filePath = promptForVideoFile();
    if (!filePath.isEmpty()) {
        showWorkWindow(filePath);
    }
}

void MainWindow::onVideoClosed() {
    if (statsWindow_) statsWindow_->hide();
    if (tagSession_) tagSession_->clear();
    showWelcomeWindow();
}

QString MainWindow::promptForVideoFile() {
    return QFileDialog::getOpenFileName(
        this,
        "Select a video file",
        QString(),
        "Video files (*.mp4 *.mov *.m4v *.mkv *.avi);;All files (*.*)"
    );
}
