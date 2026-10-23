#pragma once

#include "EventControlsSetup.h"

#include <QString>
#include <QVector>

/// Named controls templates in the application data directory.
///
/// The file is control-templates.json next to the match sessions. It holds
/// only setups the user saved. Camila's layout is the one built-in default
/// and is not written into the file, so it cannot be replaced or deleted.
/// The editor lists templates, saves the current setup under a name, and
/// applies a loaded setup with EventControlsSetup::applyToSession.
namespace EventControlsTemplateStore {

inline constexpr int kFormatVersion = 1;

struct ListedTemplate {
  QString name;
  bool builtin = false;
};

enum class FileStatus {
  Absent,
  Ready,
  Corrupt,
  NewerVersion,
  IoError,
};

struct Catalog {
  FileStatus fileStatus = FileStatus::Absent;
  QString errorMessage;
  /// The built-in layout is always first. User templates follow, sorted by name.
  QVector<ListedTemplate> templates;
};

enum class SaveStatus {
  Saved,
  InvalidName,
  BuiltinName,
  Corrupt,
  NewerVersion,
  IoError,
};

struct SaveResult {
  SaveStatus status = SaveStatus::IoError;
  QString errorMessage;
};

enum class LoadStatus {
  Loaded,
  Missing,
  Corrupt,
  NewerVersion,
  IoError,
};

struct LoadResult {
  LoadStatus status = LoadStatus::Missing;
  QString errorMessage;
  EventControlsSetup::Setup setup;
};

enum class RemoveStatus {
  Removed,
  Missing,
  BuiltinName,
  Corrupt,
  NewerVersion,
  IoError,
};

struct RemoveResult {
  RemoveStatus status = RemoveStatus::IoError;
  QString errorMessage;
};

/// Empty \p storageRoot uses the application data directory.
Catalog listTemplates(const QString& storageRoot = {});
SaveResult saveTemplate(const QString& name,
                        const EventControlsSetup::Setup& setup,
                        const QString& storageRoot = {});
LoadResult loadTemplate(const QString& name, const QString& storageRoot = {});
RemoveResult removeTemplate(const QString& name, const QString& storageRoot = {});

}  // namespace EventControlsTemplateStore
