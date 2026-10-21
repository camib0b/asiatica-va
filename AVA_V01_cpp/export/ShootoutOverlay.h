#pragma once

#include "ShootoutState.h"

#include <QImage>
#include <QString>
#include <QVector>
#include <QtGlobal>

/// Burned-in shoot-out board. It does not reuse the match-scoreboard row;
/// it sits under that image in the exporter.
struct ShootoutOverlayFrame {
    QString homeName;
    QString awayName;
    QString homeColorHex;
    QString awayColorHex;
    QString roundLabel;
    int homeConverted = 0;
    int awayConverted = 0;
    QVector<ShootoutState::MarkerKind> homeMarkers;
    QVector<ShootoutState::MarkerKind> awayMarkers;
};

struct TimedShootoutOverlay {
    double activationOffsetSeconds = 0.0;
    ShootoutOverlayFrame frame;
};

namespace ShootoutOverlay {

/// Names elide. The round label and the scores do not: if they cannot fit,
/// the whole plate scales down. Sudden-death dots past the regulation five
/// collapse from the oldest extra only when the plate would exceed
/// \p maxImageWidth. Pass 0 to skip the width cap.
QImage render(const ShootoutOverlayFrame& frame, qreal overlayScale, int maxImageWidth);

QString writeImage(const ShootoutOverlayFrame& frame,
                   const QString& outputPath,
                   qreal overlayScale,
                   int maxImageWidth);

QVector<TimedShootoutOverlay> framesForClip(const ShootoutState& state,
                                            const ShootoutState::ClipWindow& clip,
                                            AppLocale::Language language,
                                            const QString& homeName,
                                            const QString& awayName,
                                            const QString& homeColorHex,
                                            const QString& awayColorHex);

}  // namespace ShootoutOverlay
