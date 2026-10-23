#pragma once

#include <QSize>
#include <QString>
#include <QVector>

/// Builds the per-clip -filter_complex graph ClipExporter sends to ffmpeg.
/// Drawing spans are full-frame timed overlays. An empty drawing list leaves
/// the graph identical to a clip that has no drawings.

struct TimedOverlayPhase {
    double activationOffsetSeconds = 0.0;
};

struct DrawingOverlaySpan {
    double startOffsetSeconds = 0.0;
    double endOffsetSeconds = 0.0;
};

struct ClipFilterGraphInput {
    QSize outputVideoSize;
    int sourceRotationDegrees = 0;
    bool includeBottomOverlay = false;
    bool includeBrandingOverlay = false;
    int bottomOverlayLeftMargin = 0;
    int cornerMargin = 0;
    QString topSafeY;
    QString bottomSafeY;
    QString shootoutY;
    QVector<TimedOverlayPhase> scoreboards;
    QVector<TimedOverlayPhase> shootouts;
    QVector<DrawingOverlaySpan> drawings;
};

/// Input order assumed by the graph, after the source video at index 0:
/// bottom overlay, branding, scoreboards, shootouts, then one input per drawing.
QString buildClipFilterComplex(const ClipFilterGraphInput& input);
