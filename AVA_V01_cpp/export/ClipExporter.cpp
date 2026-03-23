#include "ClipExporter.h"

#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>

ClipExporter::ClipExporter(QObject* parent) : QObject(parent) {}

ClipExporter::~ClipExporter() {
    cancelExport();
    cleanup();
}

QString ClipExporter::findFfmpeg() {
    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (!fromPath.isEmpty()) return fromPath;

    const QStringList commonPaths = {
        QStringLiteral("/opt/homebrew/bin/ffmpeg"),
        QStringLiteral("/usr/local/bin/ffmpeg"),
        QStringLiteral("/usr/bin/ffmpeg"),
    };
    for (const QString& candidate : commonPaths) {
        if (QFile::exists(candidate)) return candidate;
    }
    return {};
}

void ClipExporter::setSourceVideo(const QString& path) { sourceVideoPath_ = path; }
void ClipExporter::setOutputPath(const QString& path) { outputPath_ = path; }
void ClipExporter::setClips(const QVector<ClipSegment>& clips) { clips_ = clips; }

bool ClipExporter::isRunning() const {
    return currentProcess_ && currentProcess_->state() != QProcess::NotRunning;
}

void ClipExporter::startExport() {
    ffmpegPath_ = findFfmpeg();
    if (ffmpegPath_.isEmpty()) {
        emit exportFinished(false,
            QStringLiteral("FFmpeg not found. Please install FFmpeg to export clips."));
        return;
    }

    if (sourceVideoPath_.isEmpty() || outputPath_.isEmpty() || clips_.isEmpty()) {
        emit exportFinished(false, QStringLiteral("Invalid export configuration."));
        return;
    }

    cancelled_ = false;
    currentClipIndex_ = 0;
    tempClipPaths_.clear();

    cleanup();
    tempDir_ = new QTemporaryDir();
    if (!tempDir_->isValid()) {
        emit exportFinished(false, QStringLiteral("Failed to create temporary directory."));
        cleanup();
        return;
    }

    brandingImagePath_ = generateBrandingImage(
        tempDir_->filePath(QStringLiteral("branding.png")));

    processNextClip();
}

void ClipExporter::cancelExport() {
    cancelled_ = true;
    if (currentProcess_ && currentProcess_->state() != QProcess::NotRunning) {
        currentProcess_->kill();
        currentProcess_->waitForFinished(3000);
    }
}

void ClipExporter::processNextClip() {
    if (cancelled_) {
        cleanup();
        emit exportFinished(false, QStringLiteral("Export cancelled."));
        return;
    }

    if (currentClipIndex_ >= clips_.size()) {
        concatenateClips();
        return;
    }

    emit progressChanged(currentClipIndex_ + 1, clips_.size());

    const ClipSegment& clip = clips_.at(currentClipIndex_);
    const double startSeconds = clip.startMs / 1000.0;
    const double durationSeconds = clip.durationMs / 1000.0;

    const QString tempPath = tempDir_->filePath(
        QStringLiteral("clip_%1.mp4").arg(currentClipIndex_, 4, 10, QChar('0')));

    const QString overlayImagePath = tempDir_->filePath(
        QStringLiteral("overlay_%1.png").arg(currentClipIndex_, 4, 10, QChar('0')));
    generateOverlayImage(clip.overlayText, clip.secondaryOverlayText, overlayImagePath);

    const QString filterComplex = QStringLiteral(
        "[0:v][1:v]overlay=24:main_h-overlay_h-24[tmp];"
        "[tmp][2:v]overlay=main_w-overlay_w-16:16[v]");

    QStringList arguments;
    arguments << QStringLiteral("-y")
              << QStringLiteral("-ss") << QString::number(startSeconds, 'f', 3)
              << QStringLiteral("-i") << sourceVideoPath_
              << QStringLiteral("-loop") << QStringLiteral("1")
              << QStringLiteral("-i") << overlayImagePath
              << QStringLiteral("-loop") << QStringLiteral("1")
              << QStringLiteral("-i") << brandingImagePath_
              << QStringLiteral("-filter_complex") << filterComplex
              << QStringLiteral("-map") << QStringLiteral("[v]")
              << QStringLiteral("-map") << QStringLiteral("0:a?")
              << QStringLiteral("-t") << QString::number(durationSeconds, 'f', 3)
              << QStringLiteral("-c:v") << QStringLiteral("libx264")
              << QStringLiteral("-preset") << QStringLiteral("fast")
              << QStringLiteral("-crf") << QStringLiteral("23")
              << QStringLiteral("-c:a") << QStringLiteral("aac")
              << QStringLiteral("-b:a") << QStringLiteral("128k")
              << QStringLiteral("-movflags") << QStringLiteral("+faststart")
              << tempPath;

    if (currentProcess_) {
        currentProcess_->deleteLater();
    }
    currentProcess_ = new QProcess(this);
    connect(currentProcess_,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &ClipExporter::onClipProcessFinished);

    currentProcess_->start(ffmpegPath_, arguments);
}

void ClipExporter::onClipProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (cancelled_) {
        cleanup();
        emit exportFinished(false, QStringLiteral("Export cancelled."));
        return;
    }

    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        const QString stderrOutput = currentProcess_
            ? QString::fromUtf8(currentProcess_->readAllStandardError())
            : QString();
        const QString truncated = stderrOutput.right(500);
        cleanup();
        emit exportFinished(false,
            QStringLiteral("FFmpeg failed on clip %1:\n%2")
                .arg(currentClipIndex_ + 1)
                .arg(truncated));
        return;
    }

    const QString tempPath = tempDir_->filePath(
        QStringLiteral("clip_%1.mp4").arg(currentClipIndex_, 4, 10, QChar('0')));
    tempClipPaths_.append(tempPath);

    ++currentClipIndex_;
    processNextClip();
}

void ClipExporter::concatenateClips() {
    if (cancelled_) {
        cleanup();
        emit exportFinished(false, QStringLiteral("Export cancelled."));
        return;
    }

    if (tempClipPaths_.size() == 1) {
        if (QFile::exists(outputPath_)) QFile::remove(outputPath_);
        if (QFile::copy(tempClipPaths_.first(), outputPath_)) {
            cleanup();
            emit exportFinished(true, {});
        } else {
            cleanup();
            emit exportFinished(false, QStringLiteral("Failed to copy output file."));
        }
        return;
    }

    const QString concatListPath = tempDir_->filePath(QStringLiteral("concat_list.txt"));
    QFile listFile(concatListPath);
    if (!listFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        cleanup();
        emit exportFinished(false, QStringLiteral("Failed to create concat file list."));
        return;
    }

    QTextStream stream(&listFile);
    for (const QString& clipPath : tempClipPaths_) {
        stream << QStringLiteral("file '") << clipPath << QStringLiteral("'\n");
    }
    listFile.close();

    QStringList arguments;
    arguments << QStringLiteral("-y")
              << QStringLiteral("-f") << QStringLiteral("concat")
              << QStringLiteral("-safe") << QStringLiteral("0")
              << QStringLiteral("-i") << concatListPath
              << QStringLiteral("-c") << QStringLiteral("copy")
              << outputPath_;

    if (currentProcess_) {
        currentProcess_->deleteLater();
    }
    currentProcess_ = new QProcess(this);
    connect(currentProcess_,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &ClipExporter::onConcatProcessFinished);

    currentProcess_->start(ffmpegPath_, arguments);
}

void ClipExporter::onConcatProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (cancelled_) {
        cleanup();
        emit exportFinished(false, QStringLiteral("Export cancelled."));
        return;
    }

    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        const QString stderrOutput = currentProcess_
            ? QString::fromUtf8(currentProcess_->readAllStandardError())
            : QString();
        cleanup();
        emit exportFinished(false,
            QStringLiteral("FFmpeg concat failed:\n%1").arg(stderrOutput.right(500)));
        return;
    }

    cleanup();
    emit exportFinished(true, {});
}

void ClipExporter::cleanup() {
    if (tempDir_) {
        delete tempDir_;
        tempDir_ = nullptr;
    }
    tempClipPaths_.clear();
    brandingImagePath_.clear();
}

QString ClipExporter::generateBrandingImage(const QString& outputPath) {
    constexpr int kPadding = 8;
    constexpr int kFontSize = 12;
    constexpr int kCornerRadius = 4;
    const QString brandingText = QStringLiteral("Exported from AVA");

    QFont font(QStringLiteral("Helvetica"), kFontSize);
    font.setWeight(QFont::Normal);

    const QFontMetrics metrics(font);
    const QRect textBounds = metrics.boundingRect(brandingText);

    const int imageWidth = textBounds.width() + 2 * kPadding;
    const int imageHeight = metrics.height() + 2 * kPadding;

    QImage image(imageWidth, imageHeight, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 90));
    painter.drawRoundedRect(image.rect(), kCornerRadius, kCornerRadius);

    painter.setFont(font);
    painter.setPen(QColor(255, 255, 255, 200));
    painter.drawText(image.rect(), Qt::AlignCenter, brandingText);

    painter.end();
    image.save(outputPath, "PNG");
    return outputPath;
}

QString ClipExporter::generateOverlayImage(const QString& primaryText,
                                            const QString& secondaryText,
                                            const QString& outputPath) {
    constexpr int kPadding = 16;
    constexpr int kPrimaryFontSize = 24;
    constexpr int kSecondaryFontSize = 18;
    constexpr int kLineSpacing = 6;
    constexpr int kCornerRadius = 6;

    QFont primaryFont(QStringLiteral("Helvetica"), kPrimaryFontSize);
    primaryFont.setWeight(QFont::Medium);
    const QFontMetrics primaryMetrics(primaryFont);
    const QRect primaryBounds = primaryMetrics.boundingRect(primaryText);

    const bool hasSecondary = !secondaryText.isEmpty();

    QFont secondaryFont(QStringLiteral("Helvetica"), kSecondaryFontSize);
    secondaryFont.setWeight(QFont::Normal);
    const QFontMetrics secondaryMetrics(secondaryFont);

    int contentWidth = primaryBounds.width();
    int totalTextHeight = primaryMetrics.height();

    if (hasSecondary) {
        const QRect secondaryBounds = secondaryMetrics.boundingRect(secondaryText);
        contentWidth = std::max(contentWidth, secondaryBounds.width());
        totalTextHeight += kLineSpacing + secondaryMetrics.height();
    }

    const int imageWidth = contentWidth + 2 * kPadding;
    const int imageHeight = totalTextHeight + 2 * kPadding;

    QImage image(imageWidth, imageHeight, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 115));
    painter.drawRoundedRect(image.rect(), kCornerRadius, kCornerRadius);

    const QRect primaryRect(0, kPadding, imageWidth, primaryMetrics.height());
    painter.setFont(primaryFont);
    painter.setPen(QColor(255, 255, 255));
    painter.drawText(primaryRect, Qt::AlignCenter, primaryText);

    if (hasSecondary) {
        const int secondaryY = kPadding + primaryMetrics.height() + kLineSpacing;
        const QRect secondaryRect(0, secondaryY, imageWidth, secondaryMetrics.height());
        painter.setFont(secondaryFont);
        painter.setPen(QColor(255, 255, 255, 200));
        painter.drawText(secondaryRect, Qt::AlignCenter, secondaryText);
    }

    painter.end();
    image.save(outputPath, "PNG");
    return outputPath;
}
