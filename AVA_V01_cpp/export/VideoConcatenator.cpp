#include "VideoConcatenator.h"
#include "ClipExporter.h"
#include "ConcatFileOrderDialog.h"
#include "../i18n/AppLocale.h"
#include "../style/StyleProps.h"

#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QProcess>
#include <QProgressDialog>
#include <QSignalBlocker>
#include <QTemporaryFile>
#include <QTextStream>

namespace {

bool concatPathContainsControlCharacters(const QString& path) {
    for (const QChar character : path) {
        if (character.unicode() < 0x20 || character.unicode() == 0x7F) return true;
    }
    return false;
}

QString resolvedConcatSourcePath(const QString& path) {
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) return {};
    const QString canonicalPath = info.canonicalFilePath();
    if (!canonicalPath.isEmpty()) return canonicalPath;
    return info.absoluteFilePath();
}

QString ffmpegConcatFileDirective(const QString& absolutePath) {
    QString escapedPath = absolutePath;
    escapedPath.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QStringLiteral("file '") + escapedPath + QStringLiteral("'\n");
}

QString ffmpegStderrForDisplay(const QByteArray& stderrBytes) {
    const QString text = QString::fromUtf8(stderrBytes).trimmed();
    constexpr int keepHeadCharacters = 2000;
    constexpr int keepTailCharacters = 1500;
    if (text.size() <= keepHeadCharacters + keepTailCharacters) return text;
    return text.left(keepHeadCharacters)
        + QStringLiteral("\n...\n")
        + text.right(keepTailCharacters);
}

}  // namespace

VideoConcatenator::VideoConcatenator(QObject* parent)
    : QObject(parent),
      process_(),
      concatListPath_(),
      outputPath_(),
      errorMessage_(),
      state_(JobState::Idle) {}

VideoConcatenator::~VideoConcatenator() {
    stopAndDiscardProcess();
    discardConcatList();
    if (state_ != JobState::Succeeded) removePartialOutput();
}

bool VideoConcatenator::succeeded() const {
    return state_ == JobState::Succeeded;
}

bool VideoConcatenator::isTerminal() const {
    return state_ == JobState::Succeeded
        || state_ == JobState::Failed
        || state_ == JobState::Cancelled;
}

void VideoConcatenator::beginNewJob() {
    stopAndDiscardProcess();
    discardConcatList();
    state_ = JobState::Idle;
    errorMessage_.clear();
    outputPath_.clear();
}

void VideoConcatenator::settle(JobState nextState, const QString& message) {
    if (isTerminal()) return;

    const bool hadStartedOutput = (state_ == JobState::Running);
    state_ = nextState;
    errorMessage_ = message;
    discardConcatList();
    if (nextState != JobState::Succeeded && hadStartedOutput) {
        removePartialOutput();
    }
    emit concatenationFinished(nextState == JobState::Succeeded);
}

void VideoConcatenator::stopAndDiscardProcess() {
    if (!process_) return;
    QProcess* dyingProcess = process_.release();
    dyingProcess->disconnect();
    if (dyingProcess->state() != QProcess::NotRunning) {
        dyingProcess->kill();
        dyingProcess->waitForFinished(3000);
    }
    dyingProcess->deleteLater();
}

void VideoConcatenator::discardConcatList() {
    if (concatListPath_.isEmpty()) return;
    QFile::remove(concatListPath_);
    concatListPath_.clear();
}

void VideoConcatenator::removePartialOutput() {
    if (outputPath_.isEmpty()) return;
    QFile::remove(outputPath_);
}

void VideoConcatenator::failWith(const QString& message) {
    settle(JobState::Failed, message);
}

void VideoConcatenator::startConcatenation(const QStringList& inputPaths,
                                           const QString& outputDir) {
    beginNewJob();

    const QString ffmpegPath = ClipExporter::findFfmpeg();
    if (ffmpegPath.isEmpty()) {
        failWith(AppLocale::trUi("concat.error_ffmpeg"));
        return;
    }

    QTemporaryFile listFile;
    listFile.setAutoRemove(false);
    if (!listFile.open()) {
        failWith(AppLocale::trUi("concat.error_list_file")
                     .arg(listFile.fileName(), listFile.errorString()));
        return;
    }

    QTextStream stream(&listFile);
    stream << QStringLiteral("ffconcat version 1.0\n");
    for (const QString& path : inputPaths) {
        const QString absolutePath = resolvedConcatSourcePath(path);
        if (absolutePath.isEmpty()) {
            failWith(AppLocale::trUi("concat.error_missing_file").arg(path));
            return;
        }
        if (concatPathContainsControlCharacters(absolutePath)) {
            failWith(AppLocale::trUi("concat.error_unsafe_path").arg(path));
            return;
        }
        stream << ffmpegConcatFileDirective(absolutePath);
    }
    stream.flush();
    concatListPath_ = listFile.fileName();
    if (stream.status() != QTextStream::Ok || !listFile.flush()) {
        failWith(AppLocale::trUi("concat.error_list_file")
                     .arg(concatListPath_, listFile.errorString()));
        return;
    }
    listFile.close();

    outputPath_ = outputDir + QStringLiteral("/concatenated.mp4");
    state_ = JobState::Running;

    process_ = std::make_unique<QProcess>();
    connect(process_.get(), QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &VideoConcatenator::onProcessFinished);
    connect(process_.get(), &QProcess::errorOccurred, this, &VideoConcatenator::onProcessError);

    // +faststart moves the moov atom to the file start so the OS media stack can
    // resolve duration and random-seek without scanning the whole file (critical for
    // long concatenated MP4s and smoother timeline jumps).
    QStringList arguments;
    arguments << QStringLiteral("-y")
              << QStringLiteral("-f") << QStringLiteral("concat")
              << QStringLiteral("-safe") << QStringLiteral("0")
              << QStringLiteral("-i") << concatListPath_
              << QStringLiteral("-c") << QStringLiteral("copy")
              << QStringLiteral("-movflags") << QStringLiteral("+faststart")
              << outputPath_;

    process_->start(ffmpegPath, arguments);
}

void VideoConcatenator::cancel() {
    if (isTerminal()) return;
    settle(JobState::Cancelled, QString());
    if (process_ && process_->state() != QProcess::NotRunning) {
        process_->kill();
    }
}

void VideoConcatenator::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (isTerminal()) return;

    if (exitStatus == QProcess::NormalExit && exitCode == 0) {
        settle(JobState::Succeeded, QString());
        return;
    }

    const QString stderrOutput = process_
        ? ffmpegStderrForDisplay(process_->readAllStandardError())
        : QString();
    if (stderrOutput.isEmpty()) {
        failWith(AppLocale::trUi("concat.error_failed")
            + QLatin1Char('\n')
            + QStringLiteral("no FFmpeg stderr (exit %1, status %2)")
                  .arg(exitCode)
                  .arg(exitStatus == QProcess::CrashExit
                           ? QStringLiteral("crashed")
                           : QStringLiteral("failed")));
        return;
    }
    failWith(AppLocale::trUi("concat.error_failed") + QLatin1Char('\n') + stderrOutput);
}

void VideoConcatenator::onProcessError(QProcess::ProcessError error) {
    if (isTerminal()) return;
    // Crashes and I/O errors still emit finished(); FailedToStart does not.
    if (error != QProcess::FailedToStart) return;

    const QString program = process_ ? process_->program() : QString();
    const QString processError = process_ ? process_->errorString() : QString();
    failWith(AppLocale::trUi("concat.error_ffmpeg_start").arg(program, processError));
}

bool VideoConcatenator::waitWithProgress(QWidget* parentWidget) {
    if (isTerminal()) return succeeded();

    QProgressDialog progress(
        AppLocale::trUi("concat.preparing"),
        AppLocale::trUi("concat.cancel"),
        0, 0, parentWidget);
    Style::setRole(&progress, "blocking");
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);

    QEventLoop loop;
    connect(this, &VideoConcatenator::concatenationFinished, &loop, &QEventLoop::quit);
    connect(&progress, &QProgressDialog::canceled, this, &VideoConcatenator::cancel);

    progress.show();
    if (!isTerminal()) {
        loop.exec();
    }

    const QSignalBlocker closeGuard(&progress);
    progress.close();
    return succeeded();
}

QStringList VideoConcatenator::selectVideoFiles(QWidget* parentWidget) {
    return QFileDialog::getOpenFileNames(
        parentWidget,
        AppLocale::trUi("file.select_video"),
        QString(),
        AppLocale::trUi("file.video_filter"));
}

bool VideoConcatenator::showFileOrderDialog(QStringList& filePaths,
                                            QWidget* parentWidget) {
    ConcatFileOrderDialog dialog(filePaths, parentWidget);
    if (dialog.exec() != QDialog::Accepted) return false;
    filePaths = dialog.orderedFilePaths();
    return true;
}
