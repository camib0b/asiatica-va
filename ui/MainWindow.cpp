#include "MainWindow.h"

#include <QFileDialog>
#include <QApplication>
#include <QStackedWidget>
#include <QWidget>

#include "WelcomeWindow.h"
#include "WorkWindow.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
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
    
    stack->addWidget(welcomeWindow_);
    stack->addWidget(workWindow_);
    
    // Connect signals
    connect(welcomeWindow_, &WelcomeWindow::videoImportRequested, this, &MainWindow::onVideoImportRequested);
    connect(workWindow_, &WorkWindow::videoClosed, this, &MainWindow::onVideoClosed);
    
    // Show welcome window initially
    stack->setCurrentWidget(welcomeWindow_);
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
}


void MainWindow::onVideoImportRequested() {
    const QString filePath = promptForVideoFile();
    if (!filePath.isEmpty()) {
        showWorkWindow(filePath);
    }
}

void MainWindow::onVideoClosed() {
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
