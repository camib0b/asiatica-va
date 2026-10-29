#pragma once

#include <QVector>
#include <QString>
#include <QtGlobal>

namespace XmlImporter {

struct ParsedInstance {
  qint64 startMs = 0;
  qint64 endMs = 0;
  QString code;
  QString periodLabel;
  int xmlId = 0;
};

/// Parses a LongoMatch-compatible XML file into a chronologically sorted list of instances.
/// On failure populates \p errorMessage (when non-null) with a human-readable explanation.
bool parse(const QString& filePath,
           QVector<ParsedInstance>* instances,
           QString* errorMessage = nullptr);

struct SyncAnchorResult {
  bool found = false;
  /// True when the selected instance is not the start-anchor code (Inicio).
  bool usedFallback = false;
  ParsedInstance instance;
};

/// Prefers Inicio, then Q1, then the earliest instance with a non-empty code.
/// \a found is false when \p instances is empty or every instance lacks a code.
SyncAnchorResult syncAnchorInstance(const QVector<ParsedInstance>& instances);

/// Offset that places the sync anchor's start at \p playheadMs.
/// The sync dialog proposes the current playback position as that playhead.
inline qint64 syncOffsetMsForPlayhead(qint64 playheadMs, qint64 anchorStartMs) {
  return playheadMs - anchorStartMs;
}

} // namespace XmlImporter
