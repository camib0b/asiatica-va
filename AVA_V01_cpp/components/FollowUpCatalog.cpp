#include "FollowUpCatalog.h"

#include <QHash>
#include <QSet>

namespace {

constexpr QLatin1StringView kArrowSeparator(" → ");

QString joinArrowPath(const QStringList& parts) {
  QStringList nonempty;
  nonempty.reserve(parts.size());
  for (const QString& part : parts) {
    if (!part.isEmpty()) nonempty.append(part);
  }
  return nonempty.join(kArrowSeparator);
}

const QHash<QString, QStringList>& firstLevelTable() {
  static const QHash<QString, QStringList> kTable = {
      {QStringLiteral("Shot"),
       {QStringLiteral("On target"), QStringLiteral("Off target"), QStringLiteral("Blocked")}},
      {QStringLiteral("Circle Entry"),
       {QStringLiteral("Dribbling"), QStringLiteral("Pass"), QStringLiteral("Deflection")}},
      {QStringLiteral("PC"),
       {QStringLiteral("Direct shot"), QStringLiteral("Variant"), QStringLiteral("Ruined")}},
      {QStringLiteral("16-yd"), {QStringLiteral("3 man"), QStringLiteral("4 man")}},
      {QStringLiteral("75-yd"),
       {QStringLiteral("Forward"), QStringLiteral("Sideways"), QStringLiteral("Back")}},
      {QStringLiteral("Card"),
       {QStringLiteral("Green"), QStringLiteral("Yellow"), QStringLiteral("Red")}},
      {QStringLiteral("Pass"),
       {QStringLiteral("Flick"), QStringLiteral("Push"), QStringLiteral("Sweep"), QStringLiteral("Hit")}},
      {QStringLiteral("Special"),
       {QStringLiteral("Good"), QStringLiteral("Bad"), QStringLiteral("Neutral"), QStringLiteral("Referee")}},
      {QStringLiteral("Turnover"),
       {QStringLiteral("Interception"), QStringLiteral("Tackle"), QStringLiteral("Pressure"),
        QStringLiteral("Unforced error")}},
      {QStringLiteral("PC Foul"),
       {QStringLiteral("Foot"), QStringLiteral("Stick"), QStringLiteral("Danger"), QStringLiteral("Other")}},
      {QStringLiteral("PS"), {QStringLiteral("Goal"), QStringLiteral("No Goal")}},
      {QStringLiteral("S.O."),
       {QStringLiteral("Converted"), QStringLiteral("Missed"), QStringLiteral("Replay")}},
  };
  return kTable;
}

const QHash<QString, QHash<QString, QStringList>>& secondLevelTable() {
  static const QHash<QString, QHash<QString, QStringList>> kTable = {
      {QStringLiteral("Shot"),
       {{QStringLiteral("On target"),
         {QStringLiteral("Goal"), QStringLiteral("Saved"), QStringLiteral("Post")}},
        {QStringLiteral("Off target"),
         {QStringLiteral("Closeby"), QStringLiteral("Not close")}}}},
      {QStringLiteral("PC"),
       {{QStringLiteral("Direct shot"),
         {QStringLiteral("Hit"), QStringLiteral("Swept"), QStringLiteral("Dragflick")}},
        {QStringLiteral("Variant"),
         {QStringLiteral("Goal"), QStringLiteral("No Goal"), QStringLiteral("New PC")}},
        {QStringLiteral("Ruined"),
         {QStringLiteral("Goal"), QStringLiteral("No Goal"), QStringLiteral("New PC")}}}},
      {QStringLiteral("Circle Entry"),
       {{QStringLiteral("Dribbling"),
         {QStringLiteral("Left"), QStringLiteral("Middle"), QStringLiteral("Right")}},
        {QStringLiteral("Pass"),
         {QStringLiteral("Left"), QStringLiteral("Middle"), QStringLiteral("Right")}},
        {QStringLiteral("Deflection"),
         {QStringLiteral("Left"), QStringLiteral("Middle"), QStringLiteral("Right")}}}},
      {QStringLiteral("Pass"),
       {{QStringLiteral("Flick"), {QStringLiteral("Completed"), QStringLiteral("Failed")}},
        {QStringLiteral("Push"), {QStringLiteral("Completed"), QStringLiteral("Failed")}},
        {QStringLiteral("Sweep"), {QStringLiteral("Completed"), QStringLiteral("Failed")}},
        {QStringLiteral("Hit"), {QStringLiteral("Completed"), QStringLiteral("Failed")}}}},
  };
  return kTable;
}

const QHash<QString, QHash<QString, QHash<QString, QStringList>>>& thirdLevelTable() {
  static const QHash<QString, QHash<QString, QHash<QString, QStringList>>> kTable = {
      {QStringLiteral("PC"),
       {{QStringLiteral("Direct shot"),
         {{QStringLiteral("Hit"), {QStringLiteral("Goal"), QStringLiteral("No Goal")}},
          {QStringLiteral("Swept"), {QStringLiteral("Goal"), QStringLiteral("No Goal")}},
          {QStringLiteral("Dragflick"), {QStringLiteral("Goal"), QStringLiteral("No Goal")}}}}}},
  };
  return kTable;
}

const QHash<QString, FollowUpCatalog::PayloadLayout>& payloadLayoutTable() {
  static const QHash<QString, FollowUpCatalog::PayloadLayout> kTable = {
      {QStringLiteral("50-yd"), FollowUpCatalog::PayloadLayout::TeamOnly},
      {QStringLiteral("Goal"), FollowUpCatalog::PayloadLayout::TeamOnly},
      {QStringLiteral("PC"), FollowUpCatalog::PayloadLayout::TeamThenChain},
      {QStringLiteral("PC Foul"), FollowUpCatalog::PayloadLayout::TeamThenChain},
      {QStringLiteral("PS"), FollowUpCatalog::PayloadLayout::TeamThenChain},
      {QStringLiteral("S.O."), FollowUpCatalog::PayloadLayout::TeamThenChain},
      {QStringLiteral("16-yd"), FollowUpCatalog::PayloadLayout::TeamThenChain},
      {QStringLiteral("Circle Entry"), FollowUpCatalog::PayloadLayout::TeamThenChain},
      {QStringLiteral("Card"), FollowUpCatalog::PayloadLayout::ChainThenTeam},
      {QStringLiteral("75-yd"), FollowUpCatalog::PayloadLayout::ChainThenTeam},
  };
  return kTable;
}

} // namespace

QStringList FollowUpCatalog::firstLevelOptions(const QString& mainEvent) {
  return firstLevelTable().value(mainEvent);
}

QStringList FollowUpCatalog::secondLevelOptions(const QString& mainEvent,
                                                const QString& firstFollowUp) {
  return secondLevelTable().value(mainEvent).value(firstFollowUp);
}

QStringList FollowUpCatalog::thirdLevelOptions(const QString& mainEvent,
                                               const QString& firstFollowUp,
                                               const QString& secondFollowUp) {
  return thirdLevelTable().value(mainEvent).value(firstFollowUp).value(secondFollowUp);
}

QStringList FollowUpCatalog::optionsAfter(const QString& mainEvent, const QStringList& selections) {
  if (selections.isEmpty()) return firstLevelOptions(mainEvent);
  if (selections.size() == 1) return secondLevelOptions(mainEvent, selections.at(0));
  if (selections.size() == 2) {
    return thirdLevelOptions(mainEvent, selections.at(0), selections.at(1));
  }
  return {};
}

FollowUpCatalog::PayloadLayout FollowUpCatalog::payloadLayout(const QString& mainEvent) {
  return payloadLayoutTable().value(mainEvent, PayloadLayout::PlainChain);
}

QString FollowUpCatalog::formatPayload(const QString& mainEvent, const QStringList& selections,
                                       const QString& teamLabel) {
  const QString chain = joinArrowPath(selections);
  switch (payloadLayout(mainEvent)) {
    case PayloadLayout::TeamOnly:
      return teamLabel;
    case PayloadLayout::TeamThenChain:
      if (chain.isEmpty()) return teamLabel;
      if (teamLabel.isEmpty()) return chain;
      return teamLabel + kArrowSeparator + chain;
    case PayloadLayout::ChainThenTeam:
      if (chain.isEmpty()) return teamLabel;
      if (teamLabel.isEmpty()) return chain;
      return chain + kArrowSeparator + teamLabel;
    case PayloadLayout::PlainChain:
      return chain;
  }
  return chain;
}

bool FollowUpCatalog::switchesTeamOnCommit(const QString& mainEvent) {
  static const QSet<QString> kSwitchTeamEvents = {QStringLiteral("Turnover")};
  return kSwitchTeamEvents.contains(mainEvent);
}

bool FollowUpCatalog::continuesAsGoal(const QString& mainEvent, const QStringList& selections) {
  if (selections.isEmpty() || selections.last() != QStringLiteral("Goal")) return false;
  if (mainEvent == QStringLiteral("PS") || mainEvent == QStringLiteral("PC")) return true;
  return mainEvent == QStringLiteral("Shot") &&
         selections.first() == QStringLiteral("On target");
}
