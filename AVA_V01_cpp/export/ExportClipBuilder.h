#pragma once

#include "AppLocale.h"
#include "ClipExporter.h"
#include "PresentationQueue.h"

#include <QString>
#include <QVector>

class TagSession;

namespace ExportClipBuilder {

enum class SortOrder {
    Chronological,
    ByTeamThenChronological,
};

struct OverlayOptions {
    AppLocale::Language language = AppLocale::Language::English;
    bool includeBottomOverlay = true;
    bool includeScoreboardOverlay = true;
    bool includeNotesOverlay = true;
};

QString teamDisplayName(const TagSession* session, const QString& teamKey);
QString sanitizedFileNamePart(const QString& raw);
QString eventLabelForExportFileName(const QString& canonicalEvent);
QString xmlReportBaseName(const TagSession* session);

/// Suggested compilation base name from the queued clips (no extension).
QString compilationBaseName(const TagSession* session,
                            const QVector<PresentationQueue::Clip>& clips);

QVector<PresentationQueue::Clip> sortedClips(QVector<PresentationQueue::Clip> clips,
                                             SortOrder order);

QVector<ClipSegment> buildClipSegments(const TagSession* session,
                                       const QVector<PresentationQueue::Clip>& clips,
                                       const OverlayOptions& options);

} // namespace ExportClipBuilder
