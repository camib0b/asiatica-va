#include "ClipExporter.h"

#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtGlobal>

#include <algorithm>
#include <functional>
#include <utility>

namespace {

constexpr int kReferenceVideoHeight = 720;  // Overlay sizes below are authored for 720p.
constexpr qreal kMinimumOverlayScale = 0.5;
constexpr qreal kMaximumOverlayScale = 3.0;
constexpr qreal kMaximumOverlayWidthFraction = 0.9;

class OverlayScaler {
public:
    explicit OverlayScaler(qreal factor) : factor_(factor) {}

    int pixels(qreal designPixels) const {
        return qMax(1, qRound(designPixels * factor_));
    }

    qreal points(qreal designPoints) const {
        return designPoints * factor_;
    }

private:
    qreal factor_;
};

// Reduces scale until measured width fits maxWidth. Text advance is near-linear
// in font size, so a few passes converge.
qreal fitScaleToWidth(qreal startScale, int maxWidth,
                      const std::function<int(qreal)>& measureImageWidth) {
    if (maxWidth <= 0 || startScale <= 0.0) {
        return startScale;
    }

    qreal scale = startScale;
    for (int attempt = 0; attempt < 8; ++attempt) {
        const int measuredWidth = measureImageWidth(scale);
        if (measuredWidth <= maxWidth) {
            return scale;
        }
        if (measuredWidth <= 0) {
            return scale;
        }
        scale *= static_cast<qreal>(maxWidth) / static_cast<qreal>(measuredWidth);
        scale = qMax(kMinimumOverlayScale * 0.25, scale);
    }
    return scale;
}

QSize parseSizeFromFfmpegStderr(const QString& stderrOutput) {
    // Matches forms like: Stream #0:0[0x1](und): Video: h264 ..., 3000x1688
    static const QRegularExpression sizePattern(
        QStringLiteral(R"(Stream\s+#\d+:\d+.*?Video:.*?(\d{2,5})x(\d{2,5}))"));
    QRegularExpressionMatchIterator matchIterator = sizePattern.globalMatch(stderrOutput);
    if (!matchIterator.hasNext()) {
        return {};
    }
    const QRegularExpressionMatch match = matchIterator.next();
    const int width = match.captured(1).toInt();
    const int height = match.captured(2).toInt();
    if (width <= 0 || height <= 0) {
        return {};
    }
    return QSize(width, height);
}

QSize probeWithFfprobe(const QString& ffprobePath, const QString& videoPath) {
    QProcess process;
    process.start(ffprobePath, {
        QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-select_streams"), QStringLiteral("v:0"),
        QStringLiteral("-show_entries"),
        QStringLiteral("stream=width,height:stream_side_data=rotation"),
        QStringLiteral("-of"), QStringLiteral("json"),
        videoPath,
    });
    if (!process.waitForFinished(5000)) {
        process.kill();
        process.waitForFinished(1000);
        return {};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return {};
    }

    const QJsonDocument document = QJsonDocument::fromJson(process.readAllStandardOutput());
    if (!document.isObject()) {
        return {};
    }

    const QJsonArray streams = document.object().value(QStringLiteral("streams")).toArray();
    if (streams.isEmpty() || !streams.at(0).isObject()) {
        return {};
    }

    const QJsonObject stream = streams.at(0).toObject();
    int width = stream.value(QStringLiteral("width")).toInt();
    int height = stream.value(QStringLiteral("height")).toInt();
    if (width <= 0 || height <= 0) {
        return {};
    }

    int rotationDegrees = 0;
    const QJsonArray sideDataList =
        stream.value(QStringLiteral("side_data_list")).toArray();
    for (const QJsonValue& sideDataValue : sideDataList) {
        if (!sideDataValue.isObject()) {
            continue;
        }
        const QJsonValue rotationValue =
            sideDataValue.toObject().value(QStringLiteral("rotation"));
        if (rotationValue.isDouble() || rotationValue.isString()) {
            rotationDegrees = qRound(rotationValue.toVariant().toDouble());
            break;
        }
    }

    const int absoluteRotation = qAbs(rotationDegrees) % 360;
    if (absoluteRotation == 90 || absoluteRotation == 270) {
        std::swap(width, height);
    }

    return QSize(width, height);
}

QSize probeWithFfmpeg(const QString& ffmpegPath, const QString& videoPath) {
    QProcess process;
    process.start(ffmpegPath, {
        QStringLiteral("-hide_banner"),
        QStringLiteral("-i"), videoPath,
    });
    if (!process.waitForFinished(5000)) {
        process.kill();
        process.waitForFinished(1000);
        return {};
    }
    // ffmpeg -i exits non-zero when no output is specified; stderr still has stream info.
    const QString stderrOutput = QString::fromUtf8(process.readAllStandardError());
    return parseSizeFromFfmpegStderr(stderrOutput);
}

struct BottomOverlayLayout {
    OverlayScaler scaler;
    QFont primaryFont;
    QFont secondaryFont;
    QFontMetrics primaryMetrics;
    QFontMetrics secondaryMetrics;
    int padding = 0;
    int lineSpacing = 0;
    int cornerRadius = 0;
    int contentWidth = 0;
    int totalTextHeight = 0;
    int imageWidth = 0;
    int imageHeight = 0;
    bool hasSecondary = false;

    BottomOverlayLayout()
        : scaler(1.0)
        , primaryMetrics(QFont())
        , secondaryMetrics(QFont()) {}
};

struct ScoreboardLayout {
    OverlayScaler scaler;
    QFont nameFont;
    QFont scoreFont;
    QFont sepFont;
    QFontMetrics nameMetrics;
    QFontMetrics scoreMetrics;
    QFontMetrics sepMetrics;
    int paddingH = 0;
    int paddingV = 0;
    int swatchWidth = 0;
    int swatchHeight = 0;
    int swatchRadius = 0;
    int elementSpacing = 0;
    int scoreSpacing = 0;
    int cornerRadius = 0;
    int contentWidth = 0;
    int rowHeight = 0;
    int imageWidth = 0;
    int imageHeight = 0;

    ScoreboardLayout()
        : scaler(1.0)
        , nameMetrics(QFont())
        , scoreMetrics(QFont())
        , sepMetrics(QFont()) {}
};

}  // namespace

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

QString ClipExporter::findFfprobe() {
    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    if (!fromPath.isEmpty()) return fromPath;

    const QStringList commonPaths = {
        QStringLiteral("/opt/homebrew/bin/ffprobe"),
        QStringLiteral("/usr/local/bin/ffprobe"),
        QStringLiteral("/usr/bin/ffprobe"),
    };
    for (const QString& candidate : commonPaths) {
        if (QFile::exists(candidate)) return candidate;
    }
    return {};
}

QSize ClipExporter::probeVideoDisplaySize(const QString& videoPath) {
    if (videoPath.isEmpty()) {
        return {};
    }

    const QString ffprobePath = findFfprobe();
    if (!ffprobePath.isEmpty()) {
        const QSize probedSize = probeWithFfprobe(ffprobePath, videoPath);
        if (probedSize.isValid()) {
            return probedSize;
        }
    }

    const QString ffmpegPath = findFfmpeg();
    if (!ffmpegPath.isEmpty()) {
        const QSize fallbackSize = probeWithFfmpeg(ffmpegPath, videoPath);
        if (fallbackSize.isValid()) {
            return fallbackSize;
        }
    }

    qWarning("ClipExporter: failed to probe video dimensions for %s",
             qPrintable(videoPath));
    return {};
}

qreal ClipExporter::computeOverlayScale(const QSize& videoSize) {
    if (!videoSize.isValid() || videoSize.height() <= 0) {
        return 1.0;
    }
    const qreal rawScale =
        static_cast<qreal>(videoSize.height()) / static_cast<qreal>(kReferenceVideoHeight);
    return qBound(kMinimumOverlayScale, rawScale, kMaximumOverlayScale);
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

    sourceVideoSize_ = probeVideoDisplaySize(sourceVideoPath_);
    overlayScale_ = computeOverlayScale(sourceVideoSize_);

    brandingImagePath_ = generateBrandingImage(
        tempDir_->filePath(QStringLiteral("branding.png")),
        overlayScale_);

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

    const int maxImageWidth = sourceVideoSize_.isValid()
        ? qRound(sourceVideoSize_.width() * kMaximumOverlayWidthFraction)
        : 0;

    const bool includeBottomOverlay =
        !clip.overlayText.trimmed().isEmpty() || !clip.secondaryOverlayText.trimmed().isEmpty();
    QString overlayImagePath;
    if (includeBottomOverlay) {
        overlayImagePath = tempDir_->filePath(
            QStringLiteral("overlay_%1.png").arg(currentClipIndex_, 4, 10, QChar('0')));
        generateOverlayImage(clip.overlayText, clip.secondaryOverlayText, overlayImagePath,
                             overlayScale_, maxImageWidth);
    }

    const int scoreboardCount = clip.scoreboards.size();
    QStringList scoreboardImagePaths;
    scoreboardImagePaths.reserve(scoreboardCount);
    for (int s = 0; s < scoreboardCount; ++s) {
        const QString path = tempDir_->filePath(
            QStringLiteral("scoreboard_%1_%2.png")
                .arg(currentClipIndex_, 4, 10, QChar('0'))
                .arg(s));
        generateScoreboardImage(clip.scoreboards[s].scoreboard, path,
                                overlayScale_, maxImageWidth);
        scoreboardImagePaths.append(path);
    }

    const OverlayScaler scaler(overlayScale_);
    const int bottomOverlayLeftMargin = scaler.pixels(24);
    const int bottomOverlayBottomMargin = scaler.pixels(72);
    const int cornerMargin = scaler.pixels(16);

    QStringList arguments;
    arguments << QStringLiteral("-y")
              << QStringLiteral("-ss") << QString::number(startSeconds, 'f', 3)
              << QStringLiteral("-i") << sourceVideoPath_;

    if (includeBottomOverlay) {
        arguments << QStringLiteral("-loop") << QStringLiteral("1")
                  << QStringLiteral("-i") << overlayImagePath;
    }

    arguments << QStringLiteral("-loop") << QStringLiteral("1")
              << QStringLiteral("-i") << brandingImagePath_;

    for (const QString& path : scoreboardImagePaths) {
        arguments << QStringLiteral("-loop") << QStringLiteral("1")
                  << QStringLiteral("-i") << path;
    }

    const int brandingInput = includeBottomOverlay ? 2 : 1;
    const int firstScoreboardInput = brandingInput + 1;

    QString filterComplex;
    if (includeBottomOverlay) {
        filterComplex += QStringLiteral(
            "[0:v][1:v]overlay=%1:main_h-overlay_h-%2[ov];"
            "[ov][%3:v]overlay=main_w-overlay_w-%4:%5")
            .arg(bottomOverlayLeftMargin)
            .arg(bottomOverlayBottomMargin)
            .arg(brandingInput)
            .arg(cornerMargin)
            .arg(cornerMargin);
    } else {
        filterComplex += QStringLiteral(
            "[0:v][%1:v]overlay=main_w-overlay_w-%2:%3")
            .arg(brandingInput)
            .arg(cornerMargin)
            .arg(cornerMargin);
    }

    if (scoreboardCount == 0) {
        filterComplex += QStringLiteral("[v]");
    } else if (scoreboardCount == 1) {
        filterComplex += QStringLiteral("[br];[br][%1:v]overlay=%2:%3[v]")
            .arg(firstScoreboardInput)
            .arg(cornerMargin)
            .arg(cornerMargin);
    } else {
        filterComplex += QStringLiteral("[br]");

        for (int s = 0; s < scoreboardCount; ++s) {
            const int inputIndex = firstScoreboardInput + s;
            const QString inputLabel = (s == 0)
                ? QStringLiteral("br")
                : QStringLiteral("sb%1").arg(s - 1);
            const QString outputLabel = (s == scoreboardCount - 1)
                ? QStringLiteral("v")
                : QStringLiteral("sb%1").arg(s);

            QString enableExpr;
            if (s == 0) {
                const double nextOffset =
                    clip.scoreboards[1].activationOffsetSeconds;
                enableExpr = QStringLiteral("lt(t,%1)")
                    .arg(QString::number(nextOffset, 'f', 3));
            } else if (s == scoreboardCount - 1) {
                const double thisOffset =
                    clip.scoreboards[s].activationOffsetSeconds;
                enableExpr = QStringLiteral("gte(t,%1)")
                    .arg(QString::number(thisOffset, 'f', 3));
            } else {
                const double thisOffset =
                    clip.scoreboards[s].activationOffsetSeconds;
                const double nextOffset =
                    clip.scoreboards[s + 1].activationOffsetSeconds;
                enableExpr = QStringLiteral("gte(t,%1)*lt(t,%2)")
                    .arg(QString::number(thisOffset, 'f', 3))
                    .arg(QString::number(nextOffset, 'f', 3));
            }

            filterComplex += QStringLiteral(
                ";[%1][%2:v]overlay=%3:%4:enable='%5'[%6]")
                .arg(inputLabel)
                .arg(inputIndex)
                .arg(cornerMargin)
                .arg(cornerMargin)
                .arg(enableExpr)
                .arg(outputLabel);
        }
    }

    arguments << QStringLiteral("-filter_complex") << filterComplex
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

QString ClipExporter::generateScoreboardImage(const ScoreboardOverlay& data,
                                               const QString& outputPath,
                                               qreal overlayScale,
                                               int maxImageWidth) {
    constexpr qreal kScoreboardScale = 1.15;
    const QString homeScoreStr = QString::number(data.homeGoals);
    const QString awayScoreStr = QString::number(data.awayGoals);
    const QString separator = QStringLiteral("\u2014");

    auto measureLayout = [&](qreal scale) -> ScoreboardLayout {
        ScoreboardLayout layout;
        layout.scaler = OverlayScaler(scale);
        layout.paddingH = layout.scaler.pixels(16 * kScoreboardScale);
        layout.paddingV = layout.scaler.pixels(10 * kScoreboardScale);
        layout.swatchWidth = layout.scaler.pixels(5 * kScoreboardScale);
        layout.swatchHeight = layout.scaler.pixels(22 * kScoreboardScale);
        layout.swatchRadius = layout.scaler.pixels(2 * kScoreboardScale);
        layout.elementSpacing = layout.scaler.pixels(10 * kScoreboardScale);
        layout.scoreSpacing = layout.scaler.pixels(12 * kScoreboardScale);
        layout.cornerRadius = layout.scaler.pixels(6 * kScoreboardScale);

        layout.nameFont = QFont(QStringLiteral("Helvetica"));
        layout.nameFont.setPointSizeF(layout.scaler.points(13 * kScoreboardScale));
        layout.nameFont.setWeight(QFont::DemiBold);
        layout.nameMetrics = QFontMetrics(layout.nameFont);

        layout.scoreFont = QFont(QStringLiteral("Helvetica"));
        layout.scoreFont.setPointSizeF(layout.scaler.points(22 * kScoreboardScale));
        layout.scoreFont.setWeight(QFont::Bold);
        layout.scoreMetrics = QFontMetrics(layout.scoreFont);

        layout.sepFont = QFont(QStringLiteral("Helvetica"));
        layout.sepFont.setPointSizeF(layout.scaler.points(16 * kScoreboardScale));
        layout.sepMetrics = QFontMetrics(layout.sepFont);

        layout.contentWidth = 0;
        layout.contentWidth += layout.swatchWidth + layout.elementSpacing;
        layout.contentWidth += layout.nameMetrics.horizontalAdvance(data.homeName)
            + layout.elementSpacing;
        layout.contentWidth += layout.scoreMetrics.horizontalAdvance(homeScoreStr)
            + layout.scoreSpacing;
        layout.contentWidth += layout.sepMetrics.horizontalAdvance(separator)
            + layout.scoreSpacing;
        layout.contentWidth += layout.scoreMetrics.horizontalAdvance(awayScoreStr)
            + layout.elementSpacing;
        layout.contentWidth += layout.nameMetrics.horizontalAdvance(data.awayName)
            + layout.elementSpacing;
        layout.contentWidth += layout.swatchWidth;

        layout.rowHeight = qMax(layout.nameMetrics.height(), layout.scoreMetrics.height());
        layout.imageWidth = layout.contentWidth + 2 * layout.paddingH;
        layout.imageHeight = layout.rowHeight + 2 * layout.paddingV;
        return layout;
    };

    const qreal fittedScale = fitScaleToWidth(
        overlayScale, maxImageWidth,
        [&](qreal scale) { return measureLayout(scale).imageWidth; });
    const ScoreboardLayout layout = measureLayout(fittedScale);

    QImage image(layout.imageWidth, layout.imageHeight, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    painter.setPen(Qt::NoPen);
    constexpr int kScoreboardBackgroundAlpha = 198;
    painter.setBrush(QColor(15, 23, 42, kScoreboardBackgroundAlpha));
    painter.drawRoundedRect(image.rect(), layout.cornerRadius, layout.cornerRadius);

    auto parseColor = [](const QString& hex, const QColor& fallback) -> QColor {
        QString h = hex.trimmed();
        if (!h.isEmpty() && !h.startsWith(QLatin1Char('#'))) h.prepend(QLatin1Char('#'));
        QColor c(h);
        return c.isValid() ? c : fallback;
    };

    int x = layout.paddingH;
    const int centerY = layout.imageHeight / 2;

    const QColor homeColor = parseColor(data.homeColorHex, QColor(96, 165, 250));
    painter.setBrush(homeColor);
    painter.drawRoundedRect(x, centerY - layout.swatchHeight / 2,
                            layout.swatchWidth, layout.swatchHeight,
                            layout.swatchRadius, layout.swatchRadius);
    x += layout.swatchWidth + layout.elementSpacing;

    painter.setFont(layout.nameFont);
    painter.setPen(QColor(255, 255, 255, 170));
    const int nameH = layout.nameMetrics.height();
    painter.drawText(x, centerY - nameH / 2,
                     layout.nameMetrics.horizontalAdvance(data.homeName), nameH,
                     Qt::AlignLeft | Qt::AlignVCenter, data.homeName);
    x += layout.nameMetrics.horizontalAdvance(data.homeName) + layout.elementSpacing;

    painter.setFont(layout.scoreFont);
    painter.setPen(QColor(255, 255, 255));
    const int scoreH = layout.scoreMetrics.height();
    painter.drawText(x, centerY - scoreH / 2,
                     layout.scoreMetrics.horizontalAdvance(homeScoreStr), scoreH,
                     Qt::AlignCenter, homeScoreStr);
    x += layout.scoreMetrics.horizontalAdvance(homeScoreStr) + layout.scoreSpacing;

    painter.setFont(layout.sepFont);
    painter.setPen(QColor(255, 255, 255, 90));
    const int sepH = layout.sepMetrics.height();
    painter.drawText(x, centerY - sepH / 2,
                     layout.sepMetrics.horizontalAdvance(separator), sepH,
                     Qt::AlignCenter, separator);
    x += layout.sepMetrics.horizontalAdvance(separator) + layout.scoreSpacing;

    painter.setFont(layout.scoreFont);
    painter.setPen(QColor(255, 255, 255));
    painter.drawText(x, centerY - scoreH / 2,
                     layout.scoreMetrics.horizontalAdvance(awayScoreStr), scoreH,
                     Qt::AlignCenter, awayScoreStr);
    x += layout.scoreMetrics.horizontalAdvance(awayScoreStr) + layout.elementSpacing;

    painter.setFont(layout.nameFont);
    painter.setPen(QColor(255, 255, 255, 170));
    painter.drawText(x, centerY - nameH / 2,
                     layout.nameMetrics.horizontalAdvance(data.awayName), nameH,
                     Qt::AlignLeft | Qt::AlignVCenter, data.awayName);
    x += layout.nameMetrics.horizontalAdvance(data.awayName) + layout.elementSpacing;

    const QColor awayColor = parseColor(data.awayColorHex, QColor(248, 113, 113));
    painter.setPen(Qt::NoPen);
    painter.setBrush(awayColor);
    painter.drawRoundedRect(x, centerY - layout.swatchHeight / 2,
                            layout.swatchWidth, layout.swatchHeight,
                            layout.swatchRadius, layout.swatchRadius);

    painter.end();
    image.save(outputPath, "PNG");
    return outputPath;
}

QString ClipExporter::generateBrandingImage(const QString& outputPath,
                                             qreal overlayScale) {
    constexpr double kBrandingScale = 1.3225;
    const OverlayScaler scaler(overlayScale);
    const int kPadding = scaler.pixels(8 * kBrandingScale);
    const qreal kFontPointSize = scaler.points(12.0 * kBrandingScale);
    const int kCornerRadius = scaler.pixels(4 * kBrandingScale);
    const QString brandingText = QStringLiteral("Made with AVA");

    QFont font(QStringLiteral("Helvetica"));
    font.setPointSizeF(kFontPointSize);
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
    painter.setBrush(QColor(0, 0, 0, 72));
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
                                            const QString& outputPath,
                                            qreal overlayScale,
                                            int maxImageWidth) {
    constexpr qreal kDesignPadding = 16;
    constexpr qreal kDesignPrimaryFontSize = 24;
    constexpr qreal kDesignSecondaryFontSize = 18;
    constexpr qreal kDesignLineSpacing = 6;
    constexpr qreal kDesignCornerRadius = 6;

    const bool hasSecondary = !secondaryText.isEmpty();

    auto measureLayout = [&](qreal scale) -> BottomOverlayLayout {
        BottomOverlayLayout layout;
        layout.scaler = OverlayScaler(scale);
        layout.padding = layout.scaler.pixels(kDesignPadding);
        layout.lineSpacing = layout.scaler.pixels(kDesignLineSpacing);
        layout.cornerRadius = layout.scaler.pixels(kDesignCornerRadius);
        layout.hasSecondary = hasSecondary;

        layout.primaryFont = QFont(QStringLiteral("Helvetica"));
        layout.primaryFont.setPointSizeF(layout.scaler.points(kDesignPrimaryFontSize));
        layout.primaryFont.setWeight(QFont::Medium);
        layout.primaryMetrics = QFontMetrics(layout.primaryFont);

        layout.secondaryFont = QFont(QStringLiteral("Helvetica"));
        layout.secondaryFont.setPointSizeF(layout.scaler.points(kDesignSecondaryFontSize));
        layout.secondaryFont.setWeight(QFont::Normal);
        layout.secondaryMetrics = QFontMetrics(layout.secondaryFont);

        const QRect primaryBounds = layout.primaryMetrics.boundingRect(primaryText);
        layout.contentWidth = primaryBounds.width();
        layout.totalTextHeight = layout.primaryMetrics.height();

        if (hasSecondary) {
            const QRect secondaryBounds =
                layout.secondaryMetrics.boundingRect(secondaryText);
            layout.contentWidth = std::max(layout.contentWidth, secondaryBounds.width());
            layout.totalTextHeight += layout.lineSpacing + layout.secondaryMetrics.height();
        }

        layout.imageWidth = layout.contentWidth + 2 * layout.padding;
        layout.imageHeight = layout.totalTextHeight + 2 * layout.padding;
        return layout;
    };

    const qreal fittedScale = fitScaleToWidth(
        overlayScale, maxImageWidth,
        [&](qreal scale) { return measureLayout(scale).imageWidth; });
    const BottomOverlayLayout layout = measureLayout(fittedScale);

    QImage image(layout.imageWidth, layout.imageHeight, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    painter.setPen(Qt::NoPen);
    constexpr int kPlateBackgroundAlpha = qRound(115 * 0.9);
    painter.setBrush(QColor(0, 0, 0, kPlateBackgroundAlpha));
    painter.drawRoundedRect(image.rect(), layout.cornerRadius, layout.cornerRadius);

    const QRect primaryRect(0, layout.padding, layout.imageWidth,
                            layout.primaryMetrics.height());
    painter.setFont(layout.primaryFont);
    painter.setPen(QColor(255, 255, 255));
    painter.drawText(primaryRect, Qt::AlignCenter, primaryText);

    if (layout.hasSecondary) {
        const int secondaryY =
            layout.padding + layout.primaryMetrics.height() + layout.lineSpacing;
        const QRect secondaryRect(0, secondaryY, layout.imageWidth,
                                  layout.secondaryMetrics.height());
        painter.setFont(layout.secondaryFont);
        painter.setPen(QColor(255, 255, 255, 200));
        painter.drawText(secondaryRect, Qt::AlignCenter, secondaryText);
    }

    painter.end();
    image.save(outputPath, "PNG");
    return outputPath;
}
