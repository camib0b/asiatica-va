#pragma once

#include <QString>
#include <QStringList>

/// Follow-up option tree and tag-payload policy for game events.
/// Adding an event means extending these tables, not GameControls click handlers.
class FollowUpCatalog {
public:
  enum class PayloadLayout {
    PlainChain,     ///< selections joined with " → "
    TeamThenChain,  ///< team → selections
    ChainThenTeam,  ///< selections → team
    TeamOnly,       ///< team label; ignores selections
  };

  static QStringList firstLevelOptions(const QString& mainEvent);
  static QStringList secondLevelOptions(const QString& mainEvent, const QString& firstFollowUp);
  static QStringList thirdLevelOptions(const QString& mainEvent, const QString& firstFollowUp,
                                       const QString& secondFollowUp);

  /// Options for the next stage given selections already made (empty = first level).
  static QStringList optionsAfter(const QString& mainEvent, const QStringList& selections);

  static PayloadLayout payloadLayout(const QString& mainEvent);
  static QString formatPayload(const QString& mainEvent, const QStringList& selections,
                               const QString& teamLabel);

  static bool switchesTeamOnCommit(const QString& mainEvent);
  static bool continuesAsGoal(const QString& mainEvent, const QStringList& selections);
};
