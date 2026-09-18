#include "PlaybackVideoPreparer.h"
#include "ClipExporter.h"
#include "../i18n/AppLocale.h"
#include "../style/StyleProps.h"

#include <QEventLoop>
#include <QFileInfo>
#include <QProcess>
#include <QProgressDialog>
#include <QSignalBlocker>

PlaybackVideoPreparer::PlaybackVideoPreparer(QObject* parent)
    : QObject(parent),
      process_(),
      outputPath_(),
      errorMessage_(),
      finished_(false),
      succeeded_(false),
      cancelled_(false) {}

PlaybackVideoPreparer::~PlaybackVideoPreparer() {
    stopAndDiscardProcess();
}

void PlaybackVideoPreparer::stopAndDiscardProcess() {
    if (!process_) return;
    QProcess* dyingProcess = process_.release();
    dyingProcess->disconnect();
    if (dyingProcess->state() != QProcess::NotRunning) {
        dyingProcess->kill();
        dyingProcess->waitForFinished(3000);
    }
    dyingProcess->deleteLater();
}

bool PlaybackVideoPreparer::requiresTranscodeForPlayback(const QString& filePath) {
    if (filePath.trimmed().isEmpty()) return false;

    const QString extension = QFileInfo(filePath).suffix().toLower();
    return extension == QStringLiteral("mkv") || extension == QStringLiteral("webm");
}

void PlaybackVideoPreparer::startPreparation(const QString& inputPath,
                                             const QString& outputDir) {
    const QString ffmpegPath = ClipExporter::findFfmpeg();
    if (ffmpegPath.isEmpty()) {
        finished_ = true;
        succeeded_ = false;
        errorMessage_ = AppLocale::trUi("playback_prep.error_ffmpeg");
        emit preparationFinished(false);
        return;
    }

    outputPath_ = outputDir + QStringLiteral("/playback.mp4");
    finished_ = false;
    succeeded_ = false;
    cancelled_ = false;
    errorMessage_.clear();

    stopAndDiscardProcess();
    process_ = std::make_unique<QProcess>();
    connect(process_.get(), QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &PlaybackVideoPreparer::onProcessFinished);
    connect(process_.get(), &QProcess::errorOccurred, this, &PlaybackVideoPreparer::onProcessError);

    QStringList arguments;
    arguments << QStringLiteral("-y")
              << QStringLiteral("-i") << inputPath
              << QStringLiteral("-c:v") << QStringLiteral("libx264")
              << QStringLiteral("-preset") << QStringLiteral("fast")
              << QStringLiteral("-crf") << QStringLiteral("23")
              << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
              << QStringLiteral("-c:a") << QStringLiteral("aac")
              << QStringLiteral("-b:a") << QStringLiteral("192k")
              << QStringLiteral("-movflags") << QStringLiteral("+faststart")
              << outputPath_;

    process_->start(ffmpegPath, arguments);
}

void PlaybackVideoPreparer::cancel() {
    if (finished_) return;

    cancelled_ = true;
    if (process_ && process_->state() != QProcess::NotRunning) {
        process_->kill();
    }
    finished_ = true;
    succeeded_ = false;
    errorMessage_.clear();
    emit preparationFinished(false);
}

void PlaybackVideoPreparer::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (cancelled_ || finished_) return;

    finished_ = true;
    succeeded_ = (exitStatus == QProcess::NormalExit && exitCode == 0);
    if (!succeeded_) {
        errorMessage_ = process_
            ? QString::fromUtf8(process_->readAllStandardError()).right(500)
            : AppLocale::trUi("playback_prep.error_failed");
    }
    emit preparationFinished(succeeded_);
}

void PlaybackVideoPreparer::onProcessError(QProcess::ProcessError error) {
    if (cancelled_ || finished_) return;
    // Crashes and I/O errors still emit finished(); FailedToStart does not.
    if (error != QProcess::FailedToStart) return;

    finished_ = true;
    succeeded_ = false;
    errorMessage_ = AppLocale::trUi("playback_prep.error_failed");
    if (process_ && !process_->errorString().isEmpty()) {
        errorMessage_ += QLatin1Char('\n') + process_->errorString();
    }
    emit preparationFinished(false);
}

bool PlaybackVideoPreparer::waitWithProgress(QWidget* parentWidget) {
    if (finished_) return succeeded_;

    QProgressDialog progress(
        AppLocale::trUi("playback_prep.preparing"),
        AppLocale::trUi("playback_prep.cancel"),
        0, 0, parentWidget);
    Style::setRole(&progress, "blocking");
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);

    QEventLoop loop;

    connect(this, &PlaybackVideoPreparer::preparationFinished,
            &loop, &QEventLoop::quit);
    connect(&progress, &QProgressDialog::canceled, this, &PlaybackVideoPreparer::cancel);

    progress.show();

    if (!finished_) {
        loop.exec();
    }

    const QSignalBlocker closeGuard(&progress);
    const bool preparationOk = succeeded_;
    progress.close();
    return preparationOk;
}
