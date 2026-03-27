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
#include <QTextStream>
#include <QVBoxLayout>

VideoConcatenator::VideoConcatenator(QObject* parent) : QObject(parent) {}

VideoConcatenator::~VideoConcatenator() {
    if (process_) {
        process_->disconnect();
        if (process_->state() != QProcess::NotRunning) {
            process_->kill();
            process_->waitForFinished(3000);
        }
    }
}

void VideoConcatenator::startConcatenation(const QStringList& inputPaths,
                                           const QString& outputDir) {
    const QString ffmpegPath = ClipExporter::findFfmpeg();
    if (ffmpegPath.isEmpty()) {
        finished_ = true;
        succeeded_ = false;
        errorMessage_ = AppLocale::trUi("concat.error_ffmpeg");
        emit concatenationFinished(false);
        return;
    }

    const QString concatListPath = outputDir + QStringLiteral("/concat_list.txt");
    QFile listFile(concatListPath);
    if (!listFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        finished_ = true;
        succeeded_ = false;
        errorMessage_ = AppLocale::trUi("concat.error_failed");
        emit concatenationFinished(false);
        return;
    }

    QTextStream stream(&listFile);
    for (const QString& path : inputPaths) {
        QString escapedPath = path;
        escapedPath.replace(QStringLiteral("'"), QStringLiteral("'\\''"));
        stream << QStringLiteral("file '") << escapedPath << QStringLiteral("'\n");
    }
    listFile.close();

    outputPath_ = outputDir + QStringLiteral("/concatenated.mp4");
    finished_ = false;
    succeeded_ = false;
    cancelled_ = false;
    errorMessage_.clear();

    process_ = new QProcess(this);
    connect(process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &VideoConcatenator::onProcessFinished);

    // +faststart moves the moov atom to the file start so the OS media stack can
    // resolve duration and random-seek without scanning the whole file (critical for
    // long concatenated MP4s and smoother timeline jumps).
    QStringList arguments;
    arguments << QStringLiteral("-y")
              << QStringLiteral("-f") << QStringLiteral("concat")
              << QStringLiteral("-safe") << QStringLiteral("0")
              << QStringLiteral("-i") << concatListPath
              << QStringLiteral("-c") << QStringLiteral("copy")
              << QStringLiteral("-movflags") << QStringLiteral("+faststart")
              << outputPath_;

    process_->start(ffmpegPath, arguments);
}

void VideoConcatenator::cancel() {
    cancelled_ = true;
    if (process_ && process_->state() != QProcess::NotRunning) {
        process_->kill();
    }
    finished_ = true;
    succeeded_ = false;
    errorMessage_.clear();
}

bool VideoConcatenator::isRunning() const {
    return process_ && process_->state() != QProcess::NotRunning;
}

void VideoConcatenator::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (cancelled_) return;

    finished_ = true;
    succeeded_ = (exitStatus == QProcess::NormalExit && exitCode == 0);
    if (!succeeded_) {
        errorMessage_ = process_
            ? QString::fromUtf8(process_->readAllStandardError()).right(500)
            : AppLocale::trUi("concat.error_failed");
    }
    emit concatenationFinished(succeeded_);
}

bool VideoConcatenator::waitWithProgress(QWidget* parentWidget) {
    if (finished_) return succeeded_;

    QProgressDialog progress(
        AppLocale::trUi("concat.preparing"),
        AppLocale::trUi("concat.cancel"),
        0, 0, parentWidget);
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

    progress.close();
    return succeeded_;
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
    dialog.setMinimumSize(620, 300);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setSpacing(16);
    layout->setContentsMargins(24, 24, 24, 24);

    auto* titleLabel = new QLabel(AppLocale::trUi("concat.dialog_title"), &dialog);
    Style::setRole(titleLabel, "h1");
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);

    auto* subtitleLabel = new QLabel(AppLocale::trUi("concat.dialog_subtitle"), &dialog);
    subtitleLabel->setWordWrap(true);
    subtitleLabel->setAlignment(Qt::AlignCenter);
    Style::setRole(subtitleLabel, "subhero");
    layout->addWidget(subtitleLabel);

    layout->addSpacing(4);

    auto* listWidget = new QListWidget(&dialog);
    listWidget->setFlow(QListView::LeftToRight);
    listWidget->setWrapping(false);
    listWidget->setResizeMode(QListView::Adjust);
    listWidget->setSpacing(6);
    listWidget->setDragDropMode(QAbstractItemView::InternalMove);
    listWidget->setDefaultDropAction(Qt::MoveAction);
    listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    listWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    listWidget->setFixedHeight(76);
    listWidget->setStyleSheet(QStringLiteral(
        "QListWidget { background: transparent; border: none; }"
        "QListWidget::item {"
        "  border: 1.5px solid #c8c8c8;"
        "  border-radius: 6px;"
        "  padding: 6px 14px;"
        "  background: #f5f5f5;"
        "}"
        "QListWidget::item:selected {"
        "  border: 2px solid #4a90d9;"
        "  background: #e4eefb;"
        "}"));

    for (const QString& path : filePaths) {
        auto* item = new QListWidgetItem(QFileInfo(path).fileName());
        item->setData(Qt::UserRole, path);
        item->setSizeHint(QSize(150, 52));
        item->setTextAlignment(Qt::AlignCenter);
        listWidget->addItem(item);
    }
    if (listWidget->count() > 0) listWidget->setCurrentRow(0);
    layout->addWidget(listWidget, 0, Qt::AlignCenter);

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
    continueButton->setMaximumWidth(220);
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
