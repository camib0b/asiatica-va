#include "MainWindow.h"

#include <QMessageBox>
#include <QStackedWidget>
#include <QTemporaryDir>

#include <memory>

#include "WelcomeWindow.h"
#include "WorkWindow.h"
#include "LicenseLockOverlay.h"
#include "../state/TagSession.h"
#include "../i18n/AppLocale.h"
#include "../i18n/LocaleNotifier.h"
#include "../export/ClipExporter.h"
#include "../export/VideoConcatenator.h"
#include "../license/LicenseManager.h"

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      stack_(nullptr),
      welcomeWindow_(nullptr),
      workWindow_(nullptr),
      licenseOverlay_(nullptr),
      tagSession_(std::make_unique<TagSession>()) {
    setWindowTitle(AppLocale::trUi("app.title"));
    resize(1300, 800);

    stack_ = new QStackedWidget(this);
    stack_->setObjectName("AppRoot");
    stack_->setAttribute(Qt::WA_StyledBackground, true);
    setCentralWidget(stack_);

    welcomeWindow_ = new WelcomeWindow(this);
    workWindow_ = new WorkWindow(this);
    licenseOverlay_ = new LicenseLockOverlay(this);

    workWindow_->setTagSession(tagSession_.get());

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

void MainWindow::showLicenseOverlay(bool allowClose) {
    licenseOverlay_->setCloseAllowed(allowClose);
    licenseOverlay_->applyUiStrings();
    stack_->setCurrentWidget(licenseOverlay_);
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
        showWorkWindowWithSetup(filePaths.first(), filePaths);
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

    auto tempDir = std::make_unique<QTemporaryDir>();
    if (!tempDir->isValid()) {
        QMessageBox::warning(this,
                             AppLocale::trUi("app.title"),
                             AppLocale::trUi("concat.error_failed"));
        return;
    }

    auto concatenator = std::make_unique<VideoConcatenator>();
    concatenator->startConcatenation(filePaths, tempDir->path());

    if (!concatenator->waitWithProgress(this)) {
        const QString errorMsg = concatenator->errorMessage();
        if (!errorMsg.isEmpty()) {
            QMessageBox::warning(this, AppLocale::trUi("app.title"), errorMsg);
        }
        return;
    }

    const QString concatenatedPath = concatenator->outputPath();
    concatenator.reset();

    workWindow_->setConcatenatedVideoTempDir(std::move(tempDir));
    workWindow_->setPendingConcatenation(nullptr);
    showWorkWindowWithSetup(concatenatedPath, filePaths);
}

void MainWindow::onVideoClosed() {
    resetTagSession();
    const bool entitled = LicenseManager::instance().isEntitled();
    if (entitled) {
        showWelcomeWindow();
    } else {
        showLicenseOverlay(false);
    }
}

void MainWindow::onEnterLicenseRequested() {
    const bool entitled = LicenseManager::instance().isEntitled();
    showLicenseOverlay(entitled);
}

void MainWindow::onLicenseOverlayClosed() {
    const bool entitled = LicenseManager::instance().isEntitled();
    if (entitled) {
        showWelcomeWindow();
    } else {
        showLicenseOverlay(false);
    }
}

void MainWindow::onLicenseEntitlementChanged() {
    const bool entitled = LicenseManager::instance().isEntitled();
    if (!entitled) {
        showLicenseOverlay(false);
        return;
    }
    welcomeWindow_->applyUiStrings();
    if (stack_->currentWidget() == licenseOverlay_) {
        showWelcomeWindow();
    }
}
