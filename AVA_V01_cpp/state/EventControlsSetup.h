#pragma once

#include "EventLabelOverrides.h"

#include <QJsonValue>
#include <QString>

class TagSession;

/// The controls setup a match session and a named template share.
///
/// A setup is the label, shortcut key, and hidden flag for each catalog id.
/// Follow-up edits the model supports are those three fields on a follow-up
/// id. The catalog does not yet add, remove, or reorder follow-up nodes, so
/// a setup does not carry a second tree. The editor in the next step calls
/// fromSession, applyToSession, and the template store.
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

struct ParseResult {
  bool ok = false;
  QString errorMessage;
  Setup setup;
};

/// Missing or null is an empty setup. An array that is not a valid override
/// table is rejected.
ParseResult fromJson(const QJsonValue& value);

}  // namespace EventControlsSetup
