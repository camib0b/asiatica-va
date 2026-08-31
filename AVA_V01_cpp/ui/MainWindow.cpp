#include "MainWindow.h"

#include <QApplication>
#include <QMessageBox>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QWidget>

#include "WelcomeWindow.h"
#include "WorkWindow.h"
#include "LicenseLockOverlay.h"
#include "../state/TagSession.h"
#include "../i18n/AppLocale.h"
#include "../i18n/LocaleNotifier.h"
#include "../export/ClipExporter.h"
#include "../export/VideoConcatenator.h"
#include "../license/LicenseManager.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) { // ctor-init
    setWindowTitle(AppLocale::trUi("app.title"));
    resize(1300, 800);

    stack_ = new QStackedWidget(this);
    stack_->setObjectName("AppRoot");
    stack_->setAttribute(Qt::WA_StyledBackground, true);
    setCentralWidget(stack_);

    welcomeWindow_ = new WelcomeWindow(this);
    workWindow_ = new WorkWindow(this);
    licenseOverlay_ = new LicenseLockOverlay(this);
    tagSession_ = new TagSession(this);

    if (workWindow_) workWindow_->setTagSession(tagSession_);

    stack_->addWidget(welcomeWindow_);
    stack_->addWidget(workWindow_);
    stack_->addWidget(licenseOverlay_);

    connect(welcomeWindow_, &WelcomeWindow::videoImportRequested, this, &MainWindow::onVideoImportRequested);
    connect(welcomeWindow_, &WelcomeWindow::enterLicenseRequested, this, &MainWindow::onEnterLicenseRequested);
    connect(workWindow_, &WorkWindow::videoClosed, this, &MainWindow::onVideoClosed);
    connect(licenseOverlay_, &LicenseLockOverlay::closeRequested, this, &MainWindow::onLicenseOverlayClosed);
    connect(&LicenseManager::instance(), &LicenseManager::entitlementChanged, this,
            &MainWindow::onLicenseEntitlementChanged);

    if (LicenseManager::instance().isEntitled()) {
        stack_->setCurrentWidget(welcomeWindow_);
    } else {
        showLicenseOverlay(false);
    }

    connect(&LocaleNotifier::instance(), &LocaleNotifier::languageChanged, this, [this]() {
        setWindowTitle(AppLocale::trUi("app.title"));
    });
    connect(&LocaleNotifier::instance(), &LocaleNotifier::languageChanged, welcomeWindow_,
            &WelcomeWindow::applyUiStrings);

    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
        if (tagSession_) tagSession_->clear();
        if (tagSession_) tagSession_->clearTeamInfo();
    });
}

void MainWindow::showWelcomeWindow() {
    if (stack_) stack_->setCurrentWidget(welcomeWindow_);
}

void MainWindow::showWorkWindowWithSetup(const QString& filePath) {
    if (workWindow_) workWindow_->showTeamSetupForVideo(filePath);
    if (stack_) stack_->setCurrentWidget(workWindow_);
}

void MainWindow::showLicenseOverlay(bool allowClose) {
    if (licenseOverlay_) {
        licenseOverlay_->setCloseAllowed(allowClose);
        licenseOverlay_->refreshCopy();
    }
    if (stack_) stack_->setCurrentWidget(licenseOverlay_);
}

void MainWindow::onVideoImportRequested() {
    if (!LicenseManager::instance().isEntitled()) {
        showLicenseOverlay(false);
        return;
    }

    QStringList filePaths = VideoConcatenator::selectVideoFiles(this);
    if (filePaths.isEmpty()) return;

    if (filePaths.size() == 1) {
        workWindow_->setConcatenatedVideoTempDir(nullptr);
        workWindow_->setPendingConcatenation(nullptr);
        workWindow_->setExportDefaultDirectoryFromVideoPath(filePaths.first());
        showWorkWindowWithSetup(filePaths.first());
        return;
    }

    filePaths.sort(Qt::CaseInsensitive);
    if (!VideoConcatenator::showFileOrderDialog(filePaths, this)) return;
    workWindow_->setExportDefaultDirectoryFromVideoPath(filePaths.first());

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
    if (LicenseManager::instance().isEntitled()) {
        showWelcomeWindow();
    } else {
        showLicenseOverlay(false);
    }
}

void MainWindow::onEnterLicenseRequested() {
    showLicenseOverlay(LicenseManager::instance().isEntitled());
}

void MainWindow::onLicenseOverlayClosed() {
    if (LicenseManager::instance().isEntitled()) {
        showWelcomeWindow();
    } else {
        showLicenseOverlay(false);
    }
}

void MainWindow::onLicenseEntitlementChanged() {
    if (!LicenseManager::instance().isEntitled()) {
        showLicenseOverlay(false);
        return;
    }
    if (welcomeWindow_) welcomeWindow_->applyUiStrings();
    if (stack_ && stack_->currentWidget() == licenseOverlay_) {
        showWelcomeWindow();
    }
}
