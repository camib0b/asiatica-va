#pragma once

#include "EventControlsSetup.h"
#include "EventDefaults.h"
#include "TagSession.h"
#include "Telestration.h"

#include <QDate>
#include <QString>
#include <QStringList>
#include <QVector>

class PresentationQueue;

/// Versioned JSON for one match.
///
/// The file lives in the application data directory, under sessions/, and its
/// name is the SHA-256 of the ordered canonical source-video paths. Playback
/// of a multi-file import is a temporary concatenation, and the video folder
/// may be read-only, so the session is not a sidecar beside the media.
/// `.clips.json` stays the export sidecar and is not this file.
///
/// Version 1 stores tags (follow-up ids and clip intervals included), the
/// controls setup, clip lead/lag overrides, the presentation selection, and
/// telestration drawings. A missing file means a new match. A corrupt file or
/// a newer version is not applied and is not overwritten.
namespace MatchSessionFile {

inline constexpr int kFormatVersion = 1;

enum class LoadStatus {
  Loaded,
  Missing,
  Corrupt,
  NewerVersion,
  IoError,
};

struct PresentationState {
  bool specified = false;
  QVector<quint64> selectedTagIds;
  quint64 currentTagId = 0;
};

struct Document {
  QStringList sourcePaths;
  QString homeTeamName;
  QString awayTeamName;
  QString homeTeamColor;
  QString awayTeamColor;
  QDate gameDate;
  QString homeAbbrev;
  QString awayAbbrev;
  QString matchNote;
  quint64 nextTagId = 1;
  QVector<TagSession::GameTag> tags;
  EventControlsSetup::Setup controls;
  bool clipLeadLagSpecified = false;
  QVector<EventDefaults::UserOverride> clipLeadLagOverrides;
  PresentationState presentation;
  QVector<Telestration::Drawing> drawings;
  quint64 nextDrawingId = 1;
};

struct LoadResult {
  LoadStatus status = LoadStatus::Missing;
  QString errorMessage;
  Document document;
};

enum class SaveStatus {
  Saved,
  PreservedNewerVersion,
  PreservedCorrupt,
  IoError,
};

struct SaveResult {
  SaveStatus status = SaveStatus::IoError;
  QString errorMessage;
};

enum class ExistingFileGuard {
  /// Always replace the destination.
  Replace,
  /// Leave a newer-version or unreadable file on disk.
  PreserveNewerOrCorrupt,
};

/// App data directory. Empty when the platform has none.
QString applicationDataDirectory();

/// Drops blanks. Uses the canonical path when the file exists so a relative
/// path and its absolute path name the same match.
QStringList normalizedSourcePaths(const QStringList& sourcePaths);

/// Empty when \p storageRoot or the normalized source list is empty.
QString sessionFilePath(const QString& storageRoot, const QStringList& sourcePaths);
QString sessionFilePathForSources(const QStringList& sourcePaths);

Document capture(const TagSession& session,
                 const PresentationQueue* presentationQueue,
                 const Telestration::Session* drawings,
                 const QStringList& sourcePaths);

/// Writes tags, controls, drawings, and the presentation selection into the
/// live objects. When \p keepMetadata is true, team names, colors, date, and
/// abbreviations already on \p session are left as they are (the setup form
/// just confirmed them). Clip-duration overrides are applied only when the
/// document specifies them.
void apply(const Document& document,
           TagSession& session,
           PresentationQueue* presentationQueue,
           Telestration::Session* drawings,
           bool keepMetadata);

LoadResult loadFile(const QString& filePath);
SaveResult saveFile(const QString& filePath,
                    const Document& document,
                    ExistingFileGuard guard = ExistingFileGuard::PreserveNewerOrCorrupt);

LoadResult loadForSources(const QStringList& sourcePaths);
SaveResult saveForSources(const QStringList& sourcePaths, const Document& document);

}  // namespace MatchSessionFile
