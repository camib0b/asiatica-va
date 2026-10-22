#include "FollowUpCatalog.h"

#include "AppLocale.h"
#include "EventCatalog.h"

namespace {

const EventCatalog::Node* nodeAtPath(const QString& mainEvent, const QStringList& selections) {
  const EventCatalog::Node* node = EventCatalog::mainEventForCanonicalName(mainEvent);
  if (!node) return nullptr;
  for (const QString& selection : selections) {
    node = EventCatalog::childForCanonicalName(*node, selection);
    if (!node) return nullptr;
  }
  return node;
}

QString joinArrowPath(const QStringList& parts) {
  QStringList nonempty;
  nonempty.reserve(parts.size());
  for (const QString& part : parts) {
    if (!part.isEmpty()) nonempty.append(part);
  }
  return nonempty.join(QString(AppLocale::kCompoundPathSeparator));
}

}  // namespace

QStringList FollowUpCatalog::firstLevelOptions(const QString& mainEvent) {
  return optionsAfter(mainEvent, {});
}

QStringList FollowUpCatalog::secondLevelOptions(const QString& mainEvent,
                                                const QString& firstFollowUp) {
  return optionsAfter(mainEvent, {firstFollowUp});
}

QStringList FollowUpCatalog::thirdLevelOptions(const QString& mainEvent,
                                               const QString& firstFollowUp,
                                               const QString& secondFollowUp) {
  return optionsAfter(mainEvent, {firstFollowUp, secondFollowUp});
}

QStringList FollowUpCatalog::optionsAfter(const QString& mainEvent, const QStringList& selections) {
  // A fourth question is not part of the tagging flow. Depth is capped at 3.
  if (selections.size() > 2) return {};
  const EventCatalog::Node* node = nodeAtPath(mainEvent, selections);
  if (!node) return {};
  return EventCatalog::childCanonicalNames(*node);
}

FollowUpCatalog::PayloadLayout FollowUpCatalog::payloadLayout(const QString& mainEvent) {
  return EventCatalog::payloadLayoutForMainEvent(mainEvent);
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
      return teamLabel + AppLocale::kCompoundPathSeparator + chain;
    case PayloadLayout::ChainThenTeam:
      if (chain.isEmpty()) return teamLabel;
      if (teamLabel.isEmpty()) return chain;
      return chain + AppLocale::kCompoundPathSeparator + teamLabel;
    case PayloadLayout::PlainChain:
      return chain;
  }
  return chain;
}

bool FollowUpCatalog::switchesTeamOnCommit(const QString& mainEvent) {
  return EventCatalog::switchesTeamForMainEvent(mainEvent);
}

bool FollowUpCatalog::continuesAsGoal(const QString& mainEvent, const QStringList& selections) {
  // Goal, penalty corner, and penalty stroke still chain into a Goal tag by name.
  // That special case stays here until it is described on the catalog.
  if (selections.isEmpty() || selections.last() != QStringLiteral("Goal")) return false;
  if (mainEvent == QStringLiteral("PS") || mainEvent == QStringLiteral("PC")) return true;
  return mainEvent == QStringLiteral("Shot") &&
         selections.first() == QStringLiteral("On target");
}
