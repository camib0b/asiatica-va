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
#include <utility>

namespace {

constexpr int kReferenceVideoHeight = 720;  // Overlay sizes below are authored for 720p.
constexpr int kMaximumOutputWidth = 1920;
constexpr int kMaximumOutputHeight = 1080;
constexpr qreal kMinimumOverlayScale = 0.5;
constexpr qreal kMaximumOverlayScale = 3.0;
constexpr qreal kMaximumOverlayWidthFraction = 0.9;

int evenDimension(const int value) {
    const int floored = qMax(2, value);
    return floored - (floored % 2);
}

// YouTube's standard 16:9 player paints chrome over the video itself:
// - Bottom ~8% covers the progress bar and transport controls.
// - Top ~8–10% covers the title / share / watch-later bar when visible (hover, pause, start).
// Keep our overlays clear of those bands so they stay readable on YouTube.
// Refs: title-safe guidance for YouTube player UI (bottom ~8%); classic 10% title-safe margins.
constexpr qreal kYouTubeTopSafeFraction = 0.10;
constexpr qreal kYouTubeBottomSafeFraction = 0.12;

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
    const qreal factor_;
};

// Text advance and padding scale nearly linearly with font size. Measure once
// at startScale, then apply a single proportional shrink if the plate is too wide.
qreal scaleToFitWidth(const qreal startScale, const int measuredWidth, const int maxWidth) {
    if (maxWidth <= 0 || startScale <= 0.0 || measuredWidth <= maxWidth || measuredWidth <= 0) {
        return startScale;
    }
    return qMax(kMinimumOverlayScale * 0.25,
                startScale * static_cast<qreal>(maxWidth) / static_cast<qreal>(measuredWidth));
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
    cancelled_ = true;
    stopAndDiscardProcess();
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

QSize ClipExporter::cappedOutputSize(const QSize& sourceSize) {
    if (!sourceSize.isValid() || sourceSize.width() <= 0 || sourceSize.height() <= 0) {
        return {};
    }

    const int sourceWidth = sourceSize.width();
    const int sourceHeight = sourceSize.height();
    const qreal widthScale =
        static_cast<qreal>(kMaximumOutputWidth) / static_cast<qreal>(sourceWidth);
    const qreal heightScale =
        static_cast<qreal>(kMaximumOutputHeight) / static_cast<qreal>(sourceHeight);
    const qreal fitScale = qMin(static_cast<qreal>(1.0), qMin(widthScale, heightScale));

    const int outputWidth = evenDimension(static_cast<int>(sourceWidth * fitScale));
    const int outputHeight = evenDimension(static_cast<int>(sourceHeight * fitScale));
    return QSize(outputWidth, outputHeight);
}

void ClipExporter::setSourceVideo(const QString& path) { sourceVideoPath_ = path; }
void ClipExporter::setOutputPath(const QString& path) { outputPath_ = path; }
void ClipExporter::setClips(const QVector<ClipSegment>& clips) { clips_ = clips; }
void ClipExporter::setIncludeAudioTrack(bool includeAudioTrack) {
    includeAudioTrack_ = includeAudioTrack;
}
void ClipExporter::setIncludeBrandingOverlay(bool includeBrandingOverlay) {
    includeBrandingOverlay_ = includeBrandingOverlay;
}

void ClipExporter::startExport() {
    exportFinishedEmitted_ = false;
    ffmpegPath_ = findFfmpeg();
    if (ffmpegPath_.isEmpty()) {
        finishExport(false,
            QStringLiteral("FFmpeg not found. Please install FFmpeg to export clips."));
        return;
    }

    if (sourceVideoPath_.isEmpty()) {
        finishExport(false, QStringLiteral("Source video path is empty."));
        return;
    }
    if (outputPath_.isEmpty()) {
        finishExport(false, QStringLiteral("Output path is empty."));
        return;
    }
    if (clips_.isEmpty()) {
        finishExport(false, QStringLiteral("No clips were provided for export."));
        return;
    }

    cancelled_ = false;
    currentClipIndex_ = 0;
    tempClipPaths_.clear();

    cleanup();
    tempDir_ = std::make_unique<QTemporaryDir>();
    if (!tempDir_ || !tempDir_->isValid()) {
        finishExport(false, QStringLiteral("Failed to create temporary directory."));
        return;
    }

    sourceVideoSize_ = probeVideoDisplaySize(sourceVideoPath_);
    if (!sourceVideoSize_.isValid()) {
        finishExport(false,
            QStringLiteral("Failed to probe source video dimensions for \"%1\".")
                .arg(sourceVideoPath_));
        return;
    }

    outputVideoSize_ = cappedOutputSize(sourceVideoSize_);
    if (!outputVideoSize_.isValid()) {
        finishExport(false,
            QStringLiteral("Could not compute an output size from %1x%2.")
                .arg(sourceVideoSize_.width())
                .arg(sourceVideoSize_.height()));
        return;
    }
    overlayScale_ = computeOverlayScale(outputVideoSize_);

    if (includeBrandingOverlay_) {
        brandingImagePath_ = generateBrandingImage(
            tempDir_->filePath(QStringLiteral("branding.png")),
            overlayScale_);
        if (brandingImagePath_.isEmpty()) {
            finishExport(false, QStringLiteral("Failed to write branding overlay image."));
            return;
        }
    }

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
        finishExport(false, QStringLiteral("Export cancelled."));
        return;
    }

    if (!tempDir_ || !outputVideoSize_.isValid()) {
        finishExport(false, QStringLiteral("Export state is missing a temp directory or output size."));
        return;
    }

    if (currentClipIndex_ >= clips_.size()) {
        concatenateClips();
        return;
    }

    emit progressChanged(currentClipIndex_ + 1, clips_.size());

    const ClipSegment& clip = clips_.at(currentClipIndex_);
    if (clip.durationMs <= 0) {
        finishExport(false,
            QStringLiteral("Clip %1 has invalid duration (%2 ms).")
                .arg(currentClipIndex_ + 1)
                .arg(clip.durationMs));
        return;
    }

    const double startSeconds = qMax(qint64{0}, clip.startMs) / 1000.0;
    const double durationSeconds = clip.durationMs / 1000.0;

    const QString tempPath = tempDir_->filePath(
        QStringLiteral("clip_%1.mp4").arg(currentClipIndex_, 4, 10, QChar('0')));

    const int maxImageWidth =
        qRound(outputVideoSize_.width() * kMaximumOverlayWidthFraction);

    const bool includeBottomOverlay =
        !clip.overlayText.trimmed().isEmpty() || !clip.secondaryOverlayText.trimmed().isEmpty();
    QString overlayImagePath;
    if (includeBottomOverlay) {
        overlayImagePath = tempDir_->filePath(
            QStringLiteral("overlay_%1.png").arg(currentClipIndex_, 4, 10, QChar('0')));
        if (generateOverlayImage(clip.overlayText, clip.secondaryOverlayText, overlayImagePath,
                                 overlayScale_, maxImageWidth)
                .isEmpty()) {
            finishExport(false,
                QStringLiteral("Failed to write overlay image for clip %1.")
                    .arg(currentClipIndex_ + 1));
            return;
        }
    }

    const int scoreboardCount = clip.scoreboards.size();
    QStringList scoreboardImagePaths;
    scoreboardImagePaths.reserve(scoreboardCount);
    for (int s = 0; s < scoreboardCount; ++s) {
        const QString path = tempDir_->filePath(
            QStringLiteral("scoreboard_%1_%2.png")
                .arg(currentClipIndex_, 4, 10, QChar('0'))
                .arg(s));
        if (generateScoreboardImage(clip.scoreboards[s].scoreboard, path,
                                    overlayScale_, maxImageWidth)
                .isEmpty()) {
            finishExport(false,
                QStringLiteral("Failed to write scoreboard image %1 for clip %2.")
                    .arg(s + 1)
                    .arg(currentClipIndex_ + 1));
            return;
        }
        scoreboardImagePaths.append(path);
    }

    const OverlayScaler scaler(overlayScale_);
    // Horizontal insets stay design-scaled; vertical insets use frame-height
    // fractions so they track YouTube's chrome regardless of resolution.
    const int bottomOverlayLeftMargin = scaler.pixels(24);
    const int cornerMargin = scaler.pixels(16);
    const QString topSafeY =
        QStringLiteral("main_h*%1").arg(kYouTubeTopSafeFraction, 0, 'f', 3);
    const QString bottomSafeY =
        QStringLiteral("main_h-overlay_h-main_h*%1")
            .arg(kYouTubeBottomSafeFraction, 0, 'f', 3);

    QStringList arguments;
    arguments << QStringLiteral("-y")
              << QStringLiteral("-ss") << QString::number(startSeconds, 'f', 3)
              << QStringLiteral("-i") << sourceVideoPath_;

    if (includeBottomOverlay) {
        arguments << QStringLiteral("-loop") << QStringLiteral("1")
                  << QStringLiteral("-i") << overlayImagePath;
    }

    if (includeBrandingOverlay_) {
        if (brandingImagePath_.isEmpty()) {
            finishExport(false, QStringLiteral("Branding overlay image is missing."));
            return;
        }
        arguments << QStringLiteral("-loop") << QStringLiteral("1")
                  << QStringLiteral("-i") << brandingImagePath_;
    }

    for (const QString& path : scoreboardImagePaths) {
        arguments << QStringLiteral("-loop") << QStringLiteral("1")
                  << QStringLiteral("-i") << path;
    }

    int nextInputIndex = 1;
    int bottomOverlayInput = -1;
    int brandingInput = -1;
    int firstScoreboardInput = -1;

    if (includeBottomOverlay) {
        bottomOverlayInput = nextInputIndex++;
    }
    if (includeBrandingOverlay_) {
        brandingInput = nextInputIndex++;
    }
    if (scoreboardCount > 0) {
        firstScoreboardInput = nextInputIndex;
    }

    QString filterComplex = QStringLiteral(
        "[0:v]scale=%1:%2:force_original_aspect_ratio=decrease:force_divisible_by=2,setsar=1[scaled]")
                                .arg(outputVideoSize_.width())
                                .arg(outputVideoSize_.height());
    QString currentVideoLabel = QStringLiteral("scaled");
    int stageCounter = 0;

    auto appendOverlayStage = [&filterComplex, &currentVideoLabel, &stageCounter](
                                  const int inputIndex, const QString& overlayExpression) {
        const QString nextLabel = QStringLiteral("ov%1").arg(stageCounter++);
        if (!filterComplex.isEmpty()) {
            filterComplex += QStringLiteral(";");
        }
        filterComplex += QStringLiteral("[%1][%2:v]overlay=%3[%4]")
                             .arg(currentVideoLabel)
                             .arg(inputIndex)
                             .arg(overlayExpression)
                             .arg(nextLabel);
        currentVideoLabel = nextLabel;
    };

    if (includeBottomOverlay) {
        appendOverlayStage(bottomOverlayInput,
                           QStringLiteral("%1:%2")
                               .arg(bottomOverlayLeftMargin)
                               .arg(bottomSafeY));
    }
    if (includeBrandingOverlay_) {
        appendOverlayStage(brandingInput,
                           QStringLiteral("main_w-overlay_w-%1:%2")
                               .arg(cornerMargin)
                               .arg(topSafeY));
    }

    if (scoreboardCount == 0) {
        if (!filterComplex.isEmpty()) {
            filterComplex += QStringLiteral(";");
        }
        filterComplex += QStringLiteral("[%1]null[v]").arg(currentVideoLabel);
    } else if (scoreboardCount == 1) {
        if (!filterComplex.isEmpty()) {
            filterComplex += QStringLiteral(";");
        }
        filterComplex += QStringLiteral("[%1][%2:v]overlay=%3:%4[v]")
            .arg(currentVideoLabel)
            .arg(firstScoreboardInput)
            .arg(cornerMargin)
            .arg(topSafeY);
    } else {
        for (int s = 0; s < scoreboardCount; ++s) {
            const int inputIndex = firstScoreboardInput + s;
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

            if (!filterComplex.isEmpty()) {
                filterComplex += QStringLiteral(";");
            }
            filterComplex += QStringLiteral("[%1][%2:v]overlay=%3:%4:enable='%5'[%6]")
                .arg(currentVideoLabel)
                .arg(inputIndex)
                .arg(cornerMargin)
                .arg(topSafeY)
                .arg(enableExpr)
                .arg(outputLabel);
            currentVideoLabel = outputLabel;
        }
    }

    arguments << QStringLiteral("-filter_complex") << filterComplex
              << QStringLiteral("-map") << QStringLiteral("[v]");
    if (includeAudioTrack_) {
        arguments << QStringLiteral("-map") << QStringLiteral("0:a?")
                  << QStringLiteral("-c:a") << QStringLiteral("aac")
                  << QStringLiteral("-b:a") << QStringLiteral("128k");
    } else {
        arguments << QStringLiteral("-an");
    }
    arguments << QStringLiteral("-t") << QString::number(durationSeconds, 'f', 3)
              << QStringLiteral("-c:v") << QStringLiteral("libx264")
              << QStringLiteral("-preset") << QStringLiteral("fast")
              << QStringLiteral("-crf") << QStringLiteral("23")
              << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
              << QStringLiteral("-movflags") << QStringLiteral("+faststart")
              << tempPath;

    startFfmpegJob(arguments, false);
}

void ClipExporter::onClipProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (cancelled_) {
        finishExport(false, QStringLiteral("Export cancelled."));
        return;
    }

    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        const QString stderrOutput = currentProcess_
            ? QString::fromUtf8(currentProcess_->readAllStandardError()).trimmed()
            : QString();
        const QString detail = stderrOutput.isEmpty()
            ? QStringLiteral("no FFmpeg stderr (exit %1, status %2)")
                  .arg(exitCode)
                  .arg(exitStatus == QProcess::CrashExit ? QStringLiteral("crashed")
                                                         : QStringLiteral("failed"))
            : stderrOutput.right(1500);
        finishExport(false,
            QStringLiteral("FFmpeg failed on clip %1 of %2:\n%3")
                .arg(currentClipIndex_ + 1)
                .arg(clips_.size())
                .arg(detail));
        return;
    }

    if (!tempDir_) {
        finishExport(false, QStringLiteral("Temporary directory was removed before clip %1 finished.")
                                .arg(currentClipIndex_ + 1));
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
        finishExport(false, QStringLiteral("Export cancelled."));
        return;
    }

    if (tempClipPaths_.isEmpty()) {
        finishExport(false, QStringLiteral("No rendered clips were available to concatenate."));
        return;
    }

    if (tempClipPaths_.size() == 1) {
        if (QFile::exists(outputPath_) && !QFile::remove(outputPath_)) {
            finishExport(false,
                QStringLiteral("Could not replace existing output file:\n%1").arg(outputPath_));
            return;
        }
        if (QFile::copy(tempClipPaths_.first(), outputPath_)) {
            finishExport(true, {});
        } else {
            finishExport(false,
                QStringLiteral("Failed to copy clip to output path:\n%1").arg(outputPath_));
        }
        return;
    }

    if (!tempDir_) {
        finishExport(false, QStringLiteral("Temporary directory was removed before concatenation."));
        return;
    }

    const QString concatListPath = tempDir_->filePath(QStringLiteral("concat_list.txt"));
    QFile listFile(concatListPath);
    if (!listFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        finishExport(false,
            QStringLiteral("Failed to create concat file list at \"%1\": %2")
                .arg(concatListPath, listFile.errorString()));
        return;
    }

    QTextStream stream(&listFile);
    for (const QString& clipPath : tempClipPaths_) {
        stream << QStringLiteral("file '") << clipPath << QStringLiteral("'\n");
    }
    stream.flush();
    if (stream.status() != QTextStream::Ok) {
        finishExport(false,
            QStringLiteral("Failed to write concat file list at \"%1\": %2")
                .arg(concatListPath, listFile.errorString()));
        return;
    }
    listFile.close();

    QStringList arguments;
    arguments << QStringLiteral("-y")
              << QStringLiteral("-f") << QStringLiteral("concat")
              << QStringLiteral("-safe") << QStringLiteral("0")
              << QStringLiteral("-i") << concatListPath
              << QStringLiteral("-c") << QStringLiteral("copy")
              << outputPath_;

    startFfmpegJob(arguments, true);
}

void ClipExporter::onConcatProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (cancelled_) {
        finishExport(false, QStringLiteral("Export cancelled."));
        return;
    }

    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        const QString stderrOutput = currentProcess_
            ? QString::fromUtf8(currentProcess_->readAllStandardError()).trimmed()
            : QString();
        const QString detail = stderrOutput.isEmpty()
            ? QStringLiteral("no FFmpeg stderr (exit %1, status %2)")
                  .arg(exitCode)
                  .arg(exitStatus == QProcess::CrashExit ? QStringLiteral("crashed")
                                                         : QStringLiteral("failed"))
            : stderrOutput.right(1500);
        finishExport(false, QStringLiteral("FFmpeg concat failed:\n%1").arg(detail));
        return;
    }

    finishExport(true, {});
}

void ClipExporter::cleanup() {
    stopAndDiscardProcess();
    tempDir_.reset();
    tempClipPaths_.clear();
    brandingImagePath_.clear();
}

void ClipExporter::finishExport(bool success, const QString& message) {
    if (exportFinishedEmitted_) {
        cleanup();
        return;
    }
    exportFinishedEmitted_ = true;
    emit exportFinished(success, message);
    cleanup();
}

void ClipExporter::stopAndDiscardProcess() {
    if (!currentProcess_) return;
    QProcess* dyingProcess = currentProcess_.release();
    dyingProcess->disconnect();
    if (dyingProcess->state() != QProcess::NotRunning) {
        dyingProcess->kill();
        dyingProcess->waitForFinished(3000);
    }
    dyingProcess->deleteLater();
}

void ClipExporter::startFfmpegJob(const QStringList& arguments, bool concatenating) {
    stopAndDiscardProcess();
    currentProcess_ = std::make_unique<QProcess>();
    if (concatenating) {
        connect(currentProcess_.get(),
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &ClipExporter::onConcatProcessFinished);
    } else {
        connect(currentProcess_.get(),
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &ClipExporter::onClipProcessFinished);
    }
    connect(currentProcess_.get(), &QProcess::errorOccurred, this, &ClipExporter::onProcessError);
    currentProcess_->start(ffmpegPath_, arguments);
}

void ClipExporter::onProcessError(QProcess::ProcessError error) {
    if (cancelled_ || exportFinishedEmitted_) return;
    if (error != QProcess::FailedToStart) return;

    const QString errorString = currentProcess_ ? currentProcess_->errorString() : QString();
    finishExport(false,
        errorString.isEmpty()
            ? QStringLiteral("Failed to start FFmpeg at \"%1\".").arg(ffmpegPath_)
            : QStringLiteral("Failed to start FFmpeg at \"%1\": %2").arg(ffmpegPath_, errorString));
}

QString ClipExporter::generateScoreboardImage(const ScoreboardOverlay& data,
                                               const QString& outputPath,
                                               qreal overlayScale,
                                               int maxImageWidth) {
    constexpr qreal kScoreboardScale = 1.15;
    const QString homeScoreStr = QString::number(data.homeGoals);
    const QString awayScoreStr = QString::number(data.awayGoals);
    const QString separator = QStringLiteral("\u2014");

    auto measureLayout =
        [&data, &homeScoreStr, &awayScoreStr, &separator](const qreal scale) -> ScoreboardLayout {
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

    ScoreboardLayout layout = measureLayout(overlayScale);
    if (maxImageWidth > 0 && layout.imageWidth > maxImageWidth) {
        layout = measureLayout(
            scaleToFitWidth(overlayScale, layout.imageWidth, maxImageWidth));
    }

    if (layout.imageWidth <= 0 || layout.imageHeight <= 0) return {};
    QImage image(layout.imageWidth, layout.imageHeight, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) return {};
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    painter.setPen(Qt::NoPen);
    constexpr int kScoreboardBackgroundAlpha = 198;
    painter.setBrush(QColor(15, 23, 42, kScoreboardBackgroundAlpha));
    painter.drawRoundedRect(image.rect(), layout.cornerRadius, layout.cornerRadius);

    auto parseColor = [](const QString& hex, const QColor& fallback) -> QColor {
        const QString trimmed = hex.trimmed();
        const QString normalized =
            (!trimmed.isEmpty() && !trimmed.startsWith(QLatin1Char('#')))
                ? (QLatin1Char('#') + trimmed)
                : trimmed;
        const QColor parsed(normalized);
        return parsed.isValid() ? parsed : fallback;
    };

    int x = layout.paddingH;
    const int centerY = layout.imageHeight / 2;
    const int homeNameAdvance = layout.nameMetrics.horizontalAdvance(data.homeName);
    const int awayNameAdvance = layout.nameMetrics.horizontalAdvance(data.awayName);
    const int homeScoreAdvance = layout.scoreMetrics.horizontalAdvance(homeScoreStr);
    const int awayScoreAdvance = layout.scoreMetrics.horizontalAdvance(awayScoreStr);
    const int separatorAdvance = layout.sepMetrics.horizontalAdvance(separator);

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
                     homeNameAdvance, nameH,
                     Qt::AlignLeft | Qt::AlignVCenter, data.homeName);
    x += homeNameAdvance + layout.elementSpacing;

    painter.setFont(layout.scoreFont);
    painter.setPen(QColor(255, 255, 255));
    const int scoreH = layout.scoreMetrics.height();
    painter.drawText(x, centerY - scoreH / 2,
                     homeScoreAdvance, scoreH,
                     Qt::AlignCenter, homeScoreStr);
    x += homeScoreAdvance + layout.scoreSpacing;

    painter.setFont(layout.sepFont);
    painter.setPen(QColor(255, 255, 255, 90));
    const int sepH = layout.sepMetrics.height();
    painter.drawText(x, centerY - sepH / 2,
                     separatorAdvance, sepH,
                     Qt::AlignCenter, separator);
    x += separatorAdvance + layout.scoreSpacing;

    painter.setFont(layout.scoreFont);
    painter.setPen(QColor(255, 255, 255));
    painter.drawText(x, centerY - scoreH / 2,
                     awayScoreAdvance, scoreH,
                     Qt::AlignCenter, awayScoreStr);
    x += awayScoreAdvance + layout.elementSpacing;

    painter.setFont(layout.nameFont);
    painter.setPen(QColor(255, 255, 255, 170));
    painter.drawText(x, centerY - nameH / 2,
                     awayNameAdvance, nameH,
                     Qt::AlignLeft | Qt::AlignVCenter, data.awayName);
    x += awayNameAdvance + layout.elementSpacing;

    const QColor awayColor = parseColor(data.awayColorHex, QColor(248, 113, 113));
    painter.setPen(Qt::NoPen);
    painter.setBrush(awayColor);
    painter.drawRoundedRect(x, centerY - layout.swatchHeight / 2,
                            layout.swatchWidth, layout.swatchHeight,
                            layout.swatchRadius, layout.swatchRadius);

    painter.end();

    const QString quarterText = data.periodLabel.trimmed();
    if (quarterText.isEmpty()) {
        if (!image.save(outputPath, "PNG")) return {};
        return outputPath;
    }

    const int quarterGap = layout.scaler.pixels(10 * kScoreboardScale);
    const int squareSide = layout.imageHeight;
    const int compositeWidth = squareSide + quarterGap + layout.imageWidth;

    QImage composite(compositeWidth, layout.imageHeight, QImage::Format_ARGB32_Premultiplied);
    if (composite.isNull()) return {};
    composite.fill(Qt::transparent);

    QPainter compositePainter(&composite);
    compositePainter.setRenderHint(QPainter::Antialiasing);
    compositePainter.setRenderHint(QPainter::TextAntialiasing);

    compositePainter.setPen(Qt::NoPen);
    compositePainter.setBrush(QColor(15, 23, 42, kScoreboardBackgroundAlpha));
    compositePainter.drawRoundedRect(0, 0, squareSide, squareSide,
                                     layout.cornerRadius, layout.cornerRadius);

    QFont quarterFont(QStringLiteral("Helvetica"));
    quarterFont.setWeight(QFont::Bold);
    quarterFont.setPixelSize(qMax(11, qRound(static_cast<double>(squareSide) * 0.34)));
    compositePainter.setFont(quarterFont);
    compositePainter.setPen(QColor(255, 255, 255));
    compositePainter.drawText(QRect(0, 0, squareSide, squareSide), Qt::AlignCenter, quarterText);

    compositePainter.drawImage(squareSide + quarterGap, 0, image);
    compositePainter.end();

    if (!composite.save(outputPath, "PNG")) return {};
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
    if (imageWidth <= 0 || imageHeight <= 0) return {};

    QImage image(imageWidth, imageHeight, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) return {};
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
    if (!image.save(outputPath, "PNG")) return {};
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

    auto measureLayout =
        [&primaryText, &secondaryText, hasSecondary](const qreal scale) -> BottomOverlayLayout {
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

    BottomOverlayLayout layout = measureLayout(overlayScale);
    if (maxImageWidth > 0 && layout.imageWidth > maxImageWidth) {
        layout = measureLayout(
            scaleToFitWidth(overlayScale, layout.imageWidth, maxImageWidth));
    }

    if (layout.imageWidth <= 0 || layout.imageHeight <= 0) return {};
    QImage image(layout.imageWidth, layout.imageHeight, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) return {};
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
    if (!image.save(outputPath, "PNG")) return {};
    return outputPath;
}
