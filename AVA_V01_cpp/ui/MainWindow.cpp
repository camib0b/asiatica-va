#include "MainWindow.h"

#include <QMessageBox>
#include <QStackedWidget>
#include <QTemporaryDir>

#include <memory>

#include "WelcomeWindow.h"
#include "WorkWindow.h"
#include "../state/TagSession.h"
#include "../i18n/AppLocale.h"
#include "../i18n/LocaleNotifier.h"
#include "../export/FfmpegLocator.h"
#include "../export/VideoConcatenator.h"

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      stack_(nullptr),
      welcomeWindow_(nullptr),
      workWindow_(nullptr),
      tagSession_(std::make_unique<TagSession>()) {
    setWindowTitle(AppLocale::trUi("app.title"));
    resize(1300, 800);

    stack_ = new QStackedWidget(this);
    stack_->setObjectName("AppRoot");
    stack_->setAttribute(Qt::WA_StyledBackground, true);
    setCentralWidget(stack_);

    welcomeWindow_ = new WelcomeWindow(this);
    workWindow_ = new WorkWindow(this);

    workWindow_->setTagSession(tagSession_.get());

    stack_->addWidget(welcomeWindow_);
    stack_->addWidget(workWindow_);

    connect(welcomeWindow_, &WelcomeWindow::videoImportRequested, this, &MainWindow::onVideoImportRequested);
    connect(workWindow_, &WorkWindow::videoClosed, this, &MainWindow::onVideoClosed);

    stack_->setCurrentWidget(welcomeWindow_);

    connect(&LocaleNotifier::instance(), &LocaleNotifier::languageChanged, this, [this]() {
        setWindowTitle(AppLocale::trUi("app.title"));
    });
    connect(&LocaleNotifier::instance(), &LocaleNotifier::languageChanged, welcomeWindow_,
            &WelcomeWindow::applyUiStrings);
}

MainWindow::~MainWindow() {
    workWindow_->releaseTransientResources();
    workWindow_->setTagSession(nullptr);
    tagSession_.reset();
}

void MainWindow::resetTagSession() {
    tagSession_->clear();
    tagSession_->clearGameMetadata();
}

void MainWindow::showWelcomeWindow() {
    stack_->setCurrentWidget(welcomeWindow_);
}

void MainWindow::showWorkWindowWithSetup(const QString& filePath, const QStringList& sourceVideoPaths) {
    workWindow_->showTeamSetupForVideo(filePath, sourceVideoPaths);
    stack_->setCurrentWidget(workWindow_);
}

void MainWindow::onVideoImportRequested() {
    QStringList filePaths = VideoConcatenator::selectVideoFiles(this);
    if (filePaths.isEmpty()) return;

    if (filePaths.size() == 1) {
        workWindow_->setConcatenatedVideoTempDir(nullptr);
        workWindow_->setPendingConcatenation(nullptr);
        workWindow_->setExportDefaultDirectoryFromVideoPath(filePaths.first());
        showWorkWindowWithSetup(filePaths.first(), filePaths);
        return;
    }

    filePaths.sort(Qt::CaseInsensitive);
    if (!VideoConcatenator::showFileOrderDialog(filePaths, this)) return;
    workWindow_->setExportDefaultDirectoryFromVideoPath(filePaths.first());

    const QString ffmpegPath = FfmpegLocator::findFfmpeg();
    if (ffmpegPath.isEmpty()) {
        QMessageBox::warning(this,
                             AppLocale::trUi("app.title"),
                             AppLocale::trUi("concat.error_ffmpeg"));
        return;
    }

    auto tempDir = std::make_unique<QTemporaryDir>();
    if (!tempDir->isValid()) {
        QMessageBox::warning(this,
                             AppLocale::trUi("app.title"),
                             AppLocale::trUi("concat.error_failed"));
        return;
    }

    VideoConcatenator concatenator;
    const VideoConcatenationResult result =
        concatenator.runWithProgress(filePaths, tempDir->path(), this);
    if (!result.succeeded) {
        if (!result.errorMessage.isEmpty()) {
            QMessageBox::warning(this, AppLocale::trUi("app.title"), result.errorMessage);
        }
        return;
    }

    const QString concatenatedPath = result.outputPath;

    workWindow_->setConcatenatedVideoTempDir(std::move(tempDir));
    workWindow_->setPendingConcatenation(nullptr);
    showWorkWindowWithSetup(concatenatedPath, filePaths);
}

void MainWindow::onVideoClosed() {
    resetTagSession();
    showWelcomeWindow();
}
