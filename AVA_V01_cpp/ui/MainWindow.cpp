#include "MainWindow.h"

#include <QApplication>
#include <QMessageBox>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QWidget>

#include "WelcomeWindow.h"
#include "WorkWindow.h"
#include "../state/TagSession.h"
#include "../i18n/AppLocale.h"
#include "../i18n/LocaleNotifier.h"
#include "../export/ClipExporter.h"
#include "../export/VideoConcatenator.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) { // ctor-init
    setWindowTitle(AppLocale::trUi("app.title"));
    resize(1300, 800);
    
    // Create a stacked widget to hold both windows
    auto* stack = new QStackedWidget(this);
    stack->setObjectName("AppRoot");
    stack->setAttribute(Qt::WA_StyledBackground, true);
    setCentralWidget(stack);
    
    // Create windows (game setup is embedded inside WorkWindow)
    welcomeWindow_ = new WelcomeWindow(this);
    workWindow_ = new WorkWindow(this);
    tagSession_ = new TagSession(this);

    if (workWindow_) workWindow_->setTagSession(tagSession_);
    
    stack->addWidget(welcomeWindow_);
    stack->addWidget(workWindow_);
    
    // Connect signals
    connect(welcomeWindow_, &WelcomeWindow::videoImportRequested, this, &MainWindow::onVideoImportRequested);
    connect(workWindow_, &WorkWindow::videoClosed, this, &MainWindow::onVideoClosed);
    
    // Show welcome window initially
    stack->setCurrentWidget(welcomeWindow_);

    connect(&LocaleNotifier::instance(), &LocaleNotifier::languageChanged, this, [this]() {
        setWindowTitle(AppLocale::trUi("app.title"));
    });
    connect(&LocaleNotifier::instance(), &LocaleNotifier::languageChanged, welcomeWindow_,
            &WelcomeWindow::applyUiStrings);

    // State reset on app exit (start of state management)
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
        if (tagSession_) tagSession_->clear();
        if (tagSession_) tagSession_->clearTeamInfo();
    });
}

void MainWindow::showWelcomeWindow() {
    if (auto* stack = qobject_cast<QStackedWidget*>(centralWidget())) {
        stack->setCurrentWidget(welcomeWindow_);
    }
}

void MainWindow::showWorkWindowWithSetup(const QString& filePath) {
    if (workWindow_) workWindow_->showTeamSetupForVideo(filePath);
    if (auto* stack = qobject_cast<QStackedWidget*>(centralWidget())) {
        stack->setCurrentWidget(workWindow_);
    }
}

void MainWindow::onVideoImportRequested() {
    QStringList filePaths = VideoConcatenator::selectVideoFiles(this);
    if (filePaths.isEmpty()) return;

    if (filePaths.size() == 1) {
        workWindow_->setConcatenatedVideoTempDir(nullptr);
        workWindow_->setPendingConcatenation(nullptr);
        showWorkWindowWithSetup(filePaths.first());
        return;
    }

    filePaths.sort(Qt::CaseInsensitive);
    if (!VideoConcatenator::showFileOrderDialog(filePaths, this)) return;

    const QString ffmpegPath = ClipExporter::findFfmpeg();
    if (ffmpegPath.isEmpty()) {
        QMessageBox::warning(this,
                             AppLocale::trUi("app.title"),
                             AppLocale::trUi("concat.error_ffmpeg"));
        return;
    }

    auto* tempDir = new QTemporaryDir();
    if (!tempDir->isValid()) {
        delete tempDir;
        QMessageBox::warning(this,
                             AppLocale::trUi("app.title"),
                             AppLocale::trUi("concat.error_failed"));
        return;
    }

    auto* concatenator = new VideoConcatenator(workWindow_);
    concatenator->startConcatenation(filePaths, tempDir->path());

    workWindow_->setConcatenatedVideoTempDir(tempDir);
    workWindow_->setPendingConcatenation(concatenator);
    showWorkWindowWithSetup(tempDir->filePath(QStringLiteral("concatenated.mp4")));
}

void MainWindow::onVideoClosed() {
    if (tagSession_) tagSession_->clear();
    if (tagSession_) tagSession_->clearTeamInfo();
    showWelcomeWindow();
}

