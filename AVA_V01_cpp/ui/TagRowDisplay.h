#pragma once

#include "../state/TagSession.h"

#include <QColor>
#include <QString>

/// Clock text, team label, team color, and follow-up text for a tag row.
///
/// The tags table, clip-mention list, and presentation stage share this so each
/// controller does not keep its own copy. Exported notes keep a separate team
/// rule: an empty team name stays empty there, and the follow-up path is stored
/// as written.
namespace TagRowDisplay {

QString formatTimestampMs(qint64 milliseconds);

QColor teamBackgroundForTag(const TagSession::GameTag& tag, const TagSession* tagSession);

/// Drops follow-up segments that repeat the session's team names.
/// Fixture rows still store a joined English path. Stored tags use eventLineForTag.
QString followUpPathForDisplay(const QString& followUpEvent, const TagSession* tagSession);

/// Main-event and follow-up labels from the catalog ids on \p tag.
QString eventLineForTag(const TagSession::GameTag& tag);

QString displayTeamForTag(const TagSession::GameTag& tag, const TagSession* tagSession);

}  // namespace TagRowDisplay
