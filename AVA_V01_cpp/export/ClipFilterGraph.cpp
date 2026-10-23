#include "ClipFilterGraph.h"

namespace {

QString rotationCorrectionFilter(int rotationDegrees) {
    const int normalizedRotation = ((rotationDegrees % 360) + 360) % 360;
    switch (normalizedRotation) {
    case 90:
        return QStringLiteral("transpose=1");
    case 180:
        return QStringLiteral("hflip,vflip");
    case 270:
        return QStringLiteral("transpose=2");
    default:
        return {};
    }
}

QString timedPhaseEnableExpression(double activationOffsetSeconds,
                                   bool hasFollowingPhase,
                                   double followingOffsetSeconds) {
    if (!hasFollowingPhase) {
        if (activationOffsetSeconds <= 0.0) return {};
        return QStringLiteral("gte(t,%1)").arg(QString::number(activationOffsetSeconds, 'f', 3));
    }
    return QStringLiteral("gte(t,%1)*lt(t,%2)")
        .arg(QString::number(activationOffsetSeconds, 'f', 3))
        .arg(QString::number(followingOffsetSeconds, 'f', 3));
}

QString drawingEnableExpression(double startOffsetSeconds, double endOffsetSeconds) {
    return QStringLiteral("between(t,%1,%2)")
        .arg(QString::number(startOffsetSeconds, 'f', 3))
        .arg(QString::number(endOffsetSeconds, 'f', 3));
}

}  // namespace

QString buildClipFilterComplex(const ClipFilterGraphInput& input) {
    const QString rotationFilter = rotationCorrectionFilter(input.sourceRotationDegrees);
    QString filterComplex;
    if (rotationFilter.isEmpty()) {
        filterComplex = QStringLiteral(
                            "[0:v]scale=%1:%2:force_original_aspect_ratio=decrease:force_divisible_by=2,setsar=1[scaled]")
                            .arg(input.outputVideoSize.width())
                            .arg(input.outputVideoSize.height());
    } else {
        filterComplex = QStringLiteral(
                            "[0:v]%1,scale=%2:%3:force_original_aspect_ratio=decrease:force_divisible_by=2,setsar=1[scaled]")
                            .arg(rotationFilter)
                            .arg(input.outputVideoSize.width())
                            .arg(input.outputVideoSize.height());
    }

    QString currentVideoLabel = QStringLiteral("scaled");
    int stageCounter = 0;
    int nextInputIndex = 1;
    int bottomOverlayInput = -1;
    int brandingInput = -1;
    int firstScoreboardInput = -1;
    int firstShootoutInput = -1;
    int firstDrawingInput = -1;

    if (input.includeBottomOverlay) bottomOverlayInput = nextInputIndex++;
    if (input.includeBrandingOverlay) brandingInput = nextInputIndex++;
    if (!input.scoreboards.isEmpty()) {
        firstScoreboardInput = nextInputIndex;
        nextInputIndex += input.scoreboards.size();
    }
    if (!input.shootouts.isEmpty()) {
        firstShootoutInput = nextInputIndex;
        nextInputIndex += input.shootouts.size();
    }
    if (!input.drawings.isEmpty()) {
        firstDrawingInput = nextInputIndex;
    }

    auto appendOverlayStage = [&filterComplex, &currentVideoLabel, &stageCounter](
                                  int inputIndex, const QString& overlayExpression) {
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

    if (input.includeBottomOverlay) {
        appendOverlayStage(bottomOverlayInput,
                           QStringLiteral("%1:%2")
                               .arg(input.bottomOverlayLeftMargin)
                               .arg(input.bottomSafeY));
    }
    if (input.includeBrandingOverlay) {
        appendOverlayStage(brandingInput,
                           QStringLiteral("main_w-overlay_w-%1:%2")
                               .arg(input.cornerMargin)
                               .arg(input.topSafeY));
    }

    const int scoreboardCount = input.scoreboards.size();
    const int shootoutCount = input.shootouts.size();
    const int drawingCount = input.drawings.size();
    const int timedOverlayCount = scoreboardCount + shootoutCount + drawingCount;
    int timedOverlaysApplied = 0;

    auto appendTimedOverlay = [&filterComplex,
                               &currentVideoLabel,
                               &timedOverlaysApplied,
                               timedOverlayCount](int inputIndex,
                                                  const QString& xExpression,
                                                  const QString& yExpression,
                                                  const QString& enableExpression) {
        const bool isLast = timedOverlaysApplied == timedOverlayCount - 1;
        const QString outputLabel =
            isLast ? QStringLiteral("v") : QStringLiteral("sb%1").arg(timedOverlaysApplied);
        ++timedOverlaysApplied;
        if (!filterComplex.isEmpty()) {
            filterComplex += QStringLiteral(";");
        }
        if (enableExpression.isEmpty()) {
            filterComplex += QStringLiteral("[%1][%2:v]overlay=%3:%4[%5]")
                                 .arg(currentVideoLabel)
                                 .arg(inputIndex)
                                 .arg(xExpression)
                                 .arg(yExpression)
                                 .arg(outputLabel);
        } else {
            filterComplex += QStringLiteral("[%1][%2:v]overlay=%3:%4:enable='%5'[%6]")
                                 .arg(currentVideoLabel)
                                 .arg(inputIndex)
                                 .arg(xExpression)
                                 .arg(yExpression)
                                 .arg(enableExpression)
                                 .arg(outputLabel);
        }
        currentVideoLabel = outputLabel;
    };

    if (timedOverlayCount == 0) {
        if (!filterComplex.isEmpty()) {
            filterComplex += QStringLiteral(";");
        }
        filterComplex += QStringLiteral("[%1]null[v]").arg(currentVideoLabel);
        return filterComplex;
    }

    const QString cornerX = QString::number(input.cornerMargin);
    for (int scoreboardIndex = 0; scoreboardIndex < scoreboardCount; ++scoreboardIndex) {
        const bool hasFollowingPhase = scoreboardIndex + 1 < scoreboardCount;
        const double followingOffset =
            hasFollowingPhase ? input.scoreboards.at(scoreboardIndex + 1).activationOffsetSeconds
                              : 0.0;
        appendTimedOverlay(firstScoreboardInput + scoreboardIndex,
                           cornerX,
                           input.topSafeY,
                           timedPhaseEnableExpression(
                               input.scoreboards.at(scoreboardIndex).activationOffsetSeconds,
                               hasFollowingPhase,
                               followingOffset));
    }
    for (int shootoutIndex = 0; shootoutIndex < shootoutCount; ++shootoutIndex) {
        const bool hasFollowingPhase = shootoutIndex + 1 < shootoutCount;
        const double followingOffset =
            hasFollowingPhase ? input.shootouts.at(shootoutIndex + 1).activationOffsetSeconds
                              : 0.0;
        appendTimedOverlay(firstShootoutInput + shootoutIndex,
                           cornerX,
                           input.shootoutY,
                           timedPhaseEnableExpression(
                               input.shootouts.at(shootoutIndex).activationOffsetSeconds,
                               hasFollowingPhase,
                               followingOffset));
    }
    for (int drawingIndex = 0; drawingIndex < drawingCount; ++drawingIndex) {
        const DrawingOverlaySpan& span = input.drawings.at(drawingIndex);
        appendTimedOverlay(firstDrawingInput + drawingIndex,
                           QStringLiteral("0"),
                           QStringLiteral("0"),
                           drawingEnableExpression(span.startOffsetSeconds, span.endOffsetSeconds));
    }
    return filterComplex;
}
