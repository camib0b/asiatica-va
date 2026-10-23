#pragma once

#include "EventLabelOverrides.h"

#include <QJsonValue>
#include <QString>

class TagSession;

/// The controls setup a match session and a named template share.
///
/// A setup is the label, shortcut key, and hidden flag for each catalog id,
/// plus an optional label or hidden flag for one follow-up path. An id-level
/// record applies to every instance of that id. A path record applies only to
/// that main event and follow-up chain. The catalog does not add, remove, or
/// reorder follow-up nodes, so a setup does not carry a second tree. The
/// game-controls editor calls fromSession, applyToSession, and the template store.
namespace EventControlsSetup {

struct Setup {
  EventLabelOverrides::Table labelOverrides;
};

/// Empty override table: the catalog as shipped. This is the only built-in layout.
Setup builtinDefault();
/// Stable name of builtinDefault(). It is not stored in the template file.
QString builtinDefaultName();
bool isBuiltinDefaultName(const QString& name);

Setup fromSession(const TagSession& session);

enum class ApplyStatus {
  Applied,
  Rejected,
};

/// Replaces the session's override table. Tags, notes, and drawings stay.
ApplyStatus applyToSession(TagSession& session, const Setup& setup);

QJsonArray toJson(const Setup& setup);
/// Empty when the setup has no path records. Omitted from older files.
QJsonArray pathOverridesToJson(const Setup& setup);

struct ParseResult {
  bool ok = false;
  QString errorMessage;
  Setup setup;
};

/// Missing or null \p labelOverrides is an empty id table. Missing or null
/// \p pathOverrides means there are no path records, which is how a version 1
/// file loads. An array that is not a valid override table is rejected.
ParseResult fromJson(const QJsonValue& labelOverrides,
                     const QJsonValue& pathOverrides = QJsonValue());

}  // namespace EventControlsSetup
