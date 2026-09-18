#include "VideoConcatenator.h"
#include "ClipExporter.h"
#include "../i18n/AppLocale.h"
#include "../style/StyleProps.h"

#include <QDialog>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QTemporaryFile>
#include <QTextStream>
#include <QVBoxLayout>

namespace {

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
      finished_(false),
      succeeded_(false),
      cancelled_(false) {}

VideoConcatenator::~VideoConcatenator() {
    stopAndDiscardProcess();
    discardConcatList();
    if (!succeeded_) removePartialOutput();
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
    discardConcatList();
    finished_ = true;
    succeeded_ = false;
    errorMessage_ = message;
    emit concatenationFinished(false);
}

void VideoConcatenator::startConcatenation(const QStringList& inputPaths,
                                           const QString& outputDir) {
    const QString ffmpegPath = ClipExporter::findFfmpeg();
    if (ffmpegPath.isEmpty()) {
        failWith(AppLocale::trUi("concat.error_ffmpeg"));
        return;
    }

    discardConcatList();
    QTemporaryFile listFile;
    listFile.setAutoRemove(false);
    if (!listFile.open()) {
        failWith(AppLocale::trUi("concat.error_list_file")
                     .arg(listFile.fileName(), listFile.errorString()));
        return;
    }

    QTextStream stream(&listFile);
    for (const QString& path : inputPaths) {
        QString escapedPath = path;
        escapedPath.replace(QStringLiteral("'"), QStringLiteral("'\\''"));
        stream << QStringLiteral("file '") << escapedPath << QStringLiteral("'\n");
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
    finished_ = false;
    succeeded_ = false;
    cancelled_ = false;
    errorMessage_.clear();

    stopAndDiscardProcess();
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
    // Ignore spurious cancel (e.g. QProgressDialog teardown) after a successful run;
    // otherwise succeeded_ would be cleared and callers return false incorrectly.
    if (finished_ && succeeded_) return;

    cancelled_ = true;
    if (process_ && process_->state() != QProcess::NotRunning) {
        process_->kill();
    }
    finished_ = true;
    succeeded_ = false;
    errorMessage_.clear();
    discardConcatList();
    removePartialOutput();
}

void VideoConcatenator::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (cancelled_ || finished_) return;

    finished_ = true;
    succeeded_ = (exitStatus == QProcess::NormalExit && exitCode == 0);
    discardConcatList();
    if (!succeeded_) {
        removePartialOutput();
        const QString stderrOutput = process_
            ? ffmpegStderrForDisplay(process_->readAllStandardError())
            : QString();
        if (stderrOutput.isEmpty()) {
            errorMessage_ = AppLocale::trUi("concat.error_failed")
                + QLatin1Char('\n')
                + QStringLiteral("no FFmpeg stderr (exit %1, status %2)")
                      .arg(exitCode)
                      .arg(exitStatus == QProcess::CrashExit
                               ? QStringLiteral("crashed")
                               : QStringLiteral("failed"));
        } else {
            errorMessage_ = AppLocale::trUi("concat.error_failed")
                + QLatin1Char('\n') + stderrOutput;
        }
    }
    emit concatenationFinished(succeeded_);
}

void VideoConcatenator::onProcessError(QProcess::ProcessError error) {
    if (cancelled_ || finished_) return;
    // Crashes and I/O errors still emit finished(); FailedToStart does not.
    if (error != QProcess::FailedToStart) return;

    const QString program = process_ ? process_->program() : QString();
    const QString processError = process_ ? process_->errorString() : QString();
    removePartialOutput();
    failWith(AppLocale::trUi("concat.error_ffmpeg_start").arg(program, processError));
}

bool VideoConcatenator::waitWithProgress(QWidget* parentWidget) {
    if (finished_) return succeeded_;

    QProgressDialog progress(
        AppLocale::trUi("concat.preparing"),
        AppLocale::trUi("concat.cancel"),
        0, 0, parentWidget);
    Style::setRole(&progress, "blocking");
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);

    QEventLoop loop;

    connect(this, &VideoConcatenator::concatenationFinished,
            &loop, &QEventLoop::quit);

    connect(&progress, &QProgressDialog::canceled, this, [this, &loop]() {
        cancel();
        loop.quit();
    });

    progress.show();

    if (!finished_) {
        loop.exec();
    }

    // Snapshot before close(): on some platforms closing the dialog can emit
    // canceled(), which would call cancel() and wrongly clear succeeded_.
    const bool concatenationOk = succeeded_;
    progress.close();
    return concatenationOk;
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
    QDialog dialog(parentWidget);
    dialog.setWindowTitle(AppLocale::trUi("concat.dialog_title"));
    Style::setRole(&dialog, "fileOrder");

    auto* layout = new QVBoxLayout(&dialog);
    layout->setSpacing(16);
    layout->setContentsMargins(24, 24, 24, 24);

    auto* titleLabel = new QLabel(AppLocale::trUi("concat.dialog_title"), &dialog);
    Style::setRole(titleLabel, "h1");
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);

    auto* listWidget = new QListWidget(&dialog);
    Style::setRole(listWidget, "chipStrip");
    listWidget->setFrameShape(QFrame::NoFrame);
    listWidget->setFlow(QListView::LeftToRight);
    listWidget->setWrapping(true);
    listWidget->setResizeMode(QListView::Adjust);
    listWidget->setSpacing(6);
    listWidget->setDragDropMode(QAbstractItemView::InternalMove);
    listWidget->setDefaultDropAction(Qt::MoveAction);
    listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    listWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    listWidget->setTextElideMode(Qt::ElideMiddle);

    for (const QString& path : filePaths) {
        auto* item = new QListWidgetItem(QFileInfo(path).fileName());
        item->setData(Qt::UserRole, path);
        item->setTextAlignment(Qt::AlignCenter);
        listWidget->addItem(item);
    }
    if (listWidget->count() > 0) listWidget->setCurrentRow(0);
    layout->addWidget(listWidget);

    auto* moveRow = new QHBoxLayout();
    moveRow->setSpacing(8);
    auto* moveLeftButton = new QPushButton(AppLocale::trUi("concat.move_left"), &dialog);
    auto* moveRightButton = new QPushButton(AppLocale::trUi("concat.move_right"), &dialog);
    moveLeftButton->setCursor(Qt::PointingHandCursor);
    moveRightButton->setCursor(Qt::PointingHandCursor);
    Style::setVariant(moveLeftButton, "ghost");
    Style::setSize(moveLeftButton, "sm");
    Style::setVariant(moveRightButton, "ghost");
    Style::setSize(moveRightButton, "sm");
    moveRow->addStretch(1);
    moveRow->addWidget(moveLeftButton);
    moveRow->addWidget(moveRightButton);
    moveRow->addStretch(1);
    layout->addLayout(moveRow);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(12);
    auto* cancelButton = new QPushButton(AppLocale::trUi("concat.cancel"), &dialog);
    auto* continueButton = new QPushButton(AppLocale::trUi("concat.continue_btn"), &dialog);
    cancelButton->setCursor(Qt::PointingHandCursor);
    continueButton->setCursor(Qt::PointingHandCursor);
    Style::setVariant(cancelButton, "ghost");
    Style::setSize(cancelButton, "md");
    Style::setVariant(continueButton, "welcomeImport");
    Style::setSize(continueButton, "lg");
    buttonRow->addStretch(1);
    buttonRow->addWidget(cancelButton);
    buttonRow->addWidget(continueButton);
    buttonRow->addStretch(1);
    layout->addLayout(buttonRow);

    QObject::connect(moveLeftButton, &QPushButton::clicked, &dialog, [&listWidget]() {
        const int row = listWidget->currentRow();
        if (row <= 0) return;
        QListWidgetItem* item = listWidget->takeItem(row);
        listWidget->insertItem(row - 1, item);
        listWidget->setCurrentRow(row - 1);
    });

    QObject::connect(moveRightButton, &QPushButton::clicked, &dialog, [&listWidget]() {
        const int row = listWidget->currentRow();
        if (row < 0 || row >= listWidget->count() - 1) return;
        QListWidgetItem* item = listWidget->takeItem(row);
        listWidget->insertItem(row + 1, item);
        listWidget->setCurrentRow(row + 1);
    });

    QObject::connect(continueButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    QObject::connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) return false;

    filePaths.clear();
    for (int i = 0; i < listWidget->count(); ++i) {
        filePaths.append(listWidget->item(i)->data(Qt::UserRole).toString());
    }
    return true;
}
