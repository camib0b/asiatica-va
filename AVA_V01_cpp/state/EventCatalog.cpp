#include "EventCatalog.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <array>
#include <cstddef>

namespace EventCatalog {
namespace {

template <typename... ChildIds>
constexpr Node makeFollowUp(EventId id, const char* canonicalName, const char* spanish,
                            ChildIds... childIds) {
  static_assert(sizeof...(ChildIds) <= kMaxFollowUpChildren, "too many follow-ups");
  Node node{};
  node.id = id;
  node.canonicalName = canonicalName;
  node.shortcut = Qt::Key_unknown;
  node.xmlCode = nullptr;
  node.leadMs = 0;
  node.lagMs = 0;
  node.spanish = spanish;
  node.displayLabel = nullptr;
  node.payloadLayout = PayloadLayout::PlainChain;
  node.switchesTeam = false;
  node.mainEvent = false;
  node.childCount = static_cast<int>(sizeof...(ChildIds));
  if constexpr (sizeof...(ChildIds) > 0) {
    const EventId packed[] = {childIds...};
    for (int index = 0; index < node.childCount; ++index) {
      node.children[static_cast<std::size_t>(index)] = packed[index];
    }
  }
  return node;
}

template <typename... ChildIds>
constexpr Node makeMainEvent(EventId id, const char* canonicalName, Qt::Key shortcut,
                             const char* xmlCode, qint64 leadMs, qint64 lagMs,
                             const char* spanish, PayloadLayout payloadLayout, bool switchesTeam,
                             const char* displayLabel, ChildIds... childIds) {
  static_assert(sizeof...(ChildIds) <= kMaxFollowUpChildren, "too many follow-ups");
  Node node{};
  node.id = id;
  node.canonicalName = canonicalName;
  node.shortcut = shortcut;
  node.xmlCode = xmlCode;
  node.leadMs = leadMs;
  node.lagMs = lagMs;
  node.spanish = spanish;
  node.displayLabel = displayLabel;
  node.payloadLayout = payloadLayout;
  node.switchesTeam = switchesTeam;
  node.mainEvent = true;
  node.childCount = static_cast<int>(sizeof...(ChildIds));
  if constexpr (sizeof...(ChildIds) > 0) {
    const EventId packed[] = {childIds...};
    for (int index = 0; index < node.childCount; ++index) {
      node.children[static_cast<std::size_t>(index)] = packed[index];
    }
  }
  return node;
}

// Column order for makeMainEvent: id, canonical name, key, XML code, lead ms,
// lag ms, Spanish (nullptr = same as the canonical name), payload layout,
// switches team, display label (nullptr = canonical name), then children.
// Main events stay in clip-duration settings order. QSettings groups use
// canonicalName, not the id.
const std::array<Node, 66>& catalogNodes() {
  static const std::array<Node, 66> nodes = {
      makeMainEvent(EventId::Goal, "Goal", Qt::Key_F, "GOAL", 10000, 2000, "Gol",
                    PayloadLayout::TeamOnly, false, nullptr),
      makeMainEvent(EventId::Shot, "Shot", Qt::Key_D, "SHOT", 5000, 5000, "Tiro",
                    PayloadLayout::PlainChain, false, nullptr, EventId::OnTarget,
                    EventId::OffTarget, EventId::Blocked),
      makeMainEvent(EventId::PenaltyCorner, "PC", Qt::Key_R, "PC", 2000, 8000, "Corto",
                    PayloadLayout::TeamThenChain, false, nullptr, EventId::DirectShot,
                    EventId::Variant, EventId::Ruined),
      makeMainEvent(EventId::PenaltyCornerFoul, "PC Foul", Qt::Key_S, "PCF", 5000, 2000,
                    "Falta PC", PayloadLayout::TeamThenChain, false, nullptr, EventId::Foot,
                    EventId::Stick, EventId::Danger, EventId::Other),
      makeMainEvent(EventId::Card, "Card", Qt::Key_V, "CARD", 6000, 3000, "Tarjeta",
                    PayloadLayout::ChainThenTeam, false, nullptr, EventId::Green, EventId::Yellow,
                    EventId::Red),
      makeMainEvent(EventId::Pass, "Pass", Qt::Key_Z, "PASS", 3000, 5000, "Pase",
                    PayloadLayout::PlainChain, false, nullptr, EventId::Flick, EventId::Push,
                    EventId::Sweep, EventId::PassHit),
      makeMainEvent(EventId::CircleEntry, "Circle Entry", Qt::Key_A, "ENTRY", 8000, 6000,
                    "Ingreso área", PayloadLayout::TeamThenChain, false, nullptr,
                    EventId::Dribbling, EventId::CircleEntryPass, EventId::Deflection),
      makeMainEvent(EventId::SixteenYard, "16-yd", Qt::Key_Q, "16YD", 3000, 10000, nullptr,
                    PayloadLayout::TeamThenChain, false, nullptr, EventId::ThreeMan,
                    EventId::FourMan, EventId::Fast),
      makeMainEvent(EventId::FiftyYard, "50-yd", Qt::Key_W, "50YD", 3000, 8000, nullptr,
                    PayloadLayout::TeamOnly, false, nullptr),
      makeMainEvent(EventId::SeventyFiveYard, "75-yd", Qt::Key_E, "75YD", 3000, 8000, nullptr,
                    PayloadLayout::ChainThenTeam, false, nullptr, EventId::Forward,
                    EventId::Sideways, EventId::Back),
      makeMainEvent(EventId::Turnover, "Turnover", Qt::Key_C, "TO", 4000, 4000, "Pérdida",
                    PayloadLayout::PlainChain, true, nullptr, EventId::Interception,
                    EventId::Tackle, EventId::Pressure, EventId::UnforcedError),
      makeMainEvent(EventId::Special, "Special", Qt::Key_X, "SPC", 4000, 4000, nullptr,
                    PayloadLayout::PlainChain, false, "☆", EventId::Good, EventId::Bad,
                    EventId::Neutral, EventId::Referee),
      makeMainEvent(EventId::PenaltyStroke, "PS", Qt::Key_B, "PS", 3000, 4000, "Penal",
                    PayloadLayout::TeamThenChain, false, nullptr, EventId::FollowUpGoal,
                    EventId::NoGoal),
      makeMainEvent(EventId::Shootout, "S.O.", Qt::Key_N, "SO", 2000, 9000, nullptr,
                    PayloadLayout::TeamThenChain, false, nullptr, EventId::Converted,
                    EventId::Missed, EventId::Replay),

      makeFollowUp(EventId::OnTarget, "On target", "Al arco", EventId::FollowUpGoal, EventId::Saved,
                   EventId::Post),
      makeFollowUp(EventId::OffTarget, "Off target", "Afuera", EventId::Closeby, EventId::NotClose),
      makeFollowUp(EventId::Blocked, "Blocked", "Bloqueado"),
      makeFollowUp(EventId::Saved, "Saved", "Atajado"),
      makeFollowUp(EventId::Post, "Post", "Poste"),
      makeFollowUp(EventId::Closeby, "Closeby", "Cerca"),
      makeFollowUp(EventId::NotClose, "Not close", "Lejos"),
      makeFollowUp(EventId::FollowUpGoal, "Goal", "Gol"),
      makeFollowUp(EventId::NoGoal, "No Goal", nullptr),
      makeFollowUp(EventId::Dribbling, "Dribbling", "Conducción", EventId::Left, EventId::Middle,
                   EventId::Right),
      makeFollowUp(EventId::CircleEntryPass, "Pass", "Pase", EventId::Left, EventId::Middle,
                   EventId::Right),
      makeFollowUp(EventId::Deflection, "Deflection", "Desvío", EventId::Left, EventId::Middle,
                   EventId::Right),
      makeFollowUp(EventId::Left, "Left", "Izquierda"),
      makeFollowUp(EventId::Middle, "Middle", "Centro"),
      makeFollowUp(EventId::Right, "Right", "Derecha"),
      makeFollowUp(EventId::DirectShot, "Direct shot", "Directo", EventId::DirectShotHit,
                   EventId::Swept, EventId::Dragflick),
      makeFollowUp(EventId::Variant, "Variant", "Variante", EventId::FollowUpGoal, EventId::NoGoal,
                   EventId::NewPc),
      makeFollowUp(EventId::Ruined, "Ruined", "Arruinado", EventId::FollowUpGoal, EventId::NoGoal,
                   EventId::NewPc),
      makeFollowUp(EventId::DirectShotHit, "Hit", "Pegada", EventId::FollowUpGoal, EventId::NoGoal),
      makeFollowUp(EventId::Swept, "Swept", "Sweep", EventId::FollowUpGoal, EventId::NoGoal),
      makeFollowUp(EventId::Dragflick, "Dragflick", "Arrastre", EventId::FollowUpGoal,
                   EventId::NoGoal),
      makeFollowUp(EventId::NewPc, "New PC", "Nuevo corto"),
      makeFollowUp(EventId::ThreeMan, "3 man", "de 3"),
      makeFollowUp(EventId::FourMan, "4 man", "de 4"),
      makeFollowUp(EventId::Fast, "Fast", "rápida"),
      makeFollowUp(EventId::Forward, "Forward", "Hacia adelante"),
      makeFollowUp(EventId::Sideways, "Sideways", "Hacia el lado"),
      makeFollowUp(EventId::Back, "Back", "Hacia atrás"),
      makeFollowUp(EventId::Green, "Green", "Verde"),
      makeFollowUp(EventId::Yellow, "Yellow", "Amarilla"),
      makeFollowUp(EventId::Red, "Red", "Roja"),
      makeFollowUp(EventId::Flick, "Flick", nullptr, EventId::Completed, EventId::Failed),
      makeFollowUp(EventId::Push, "Push", nullptr, EventId::Completed, EventId::Failed),
      makeFollowUp(EventId::Sweep, "Sweep", nullptr, EventId::Completed, EventId::Failed),
      makeFollowUp(EventId::PassHit, "Hit", "Pegada", EventId::Completed, EventId::Failed),
      makeFollowUp(EventId::Completed, "Completed", "Completado"),
      makeFollowUp(EventId::Failed, "Failed", "Fallido"),
      makeFollowUp(EventId::Good, "Good", "Positivo"),
      makeFollowUp(EventId::Bad, "Bad", "Negativo"),
      makeFollowUp(EventId::Neutral, "Neutral", "Neutro"),
      makeFollowUp(EventId::Referee, "Referee", "Arbitraje"),
      makeFollowUp(EventId::Interception, "Interception", "Intercepción"),
      makeFollowUp(EventId::Tackle, "Tackle", "Quite"),
      makeFollowUp(EventId::Pressure, "Pressure", "Presión"),
      makeFollowUp(EventId::UnforcedError, "Unforced error", "Error"),
      makeFollowUp(EventId::Foot, "Foot", "Pie"),
      makeFollowUp(EventId::Stick, "Stick", "Palo"),
      makeFollowUp(EventId::Danger, "Danger", "Peligro"),
      makeFollowUp(EventId::Other, "Other", "Otro"),
      makeFollowUp(EventId::Converted, "Converted", "Convertido"),
      makeFollowUp(EventId::Missed, "Missed", "No convertido"),
      makeFollowUp(EventId::Replay, "Replay", "Repite"),
  };
  return nodes;
}

struct CatalogIndex {
  QHash<int, int> idToNodeIndex;
  QHash<QString, int> mainNameToNodeIndex;
  QHash<QString, QString> spanishByCanonicalName;
};

int followUpDepthFrom(const CatalogIndex& index, EventId id, QSet<int>* stack) {
  const int idValue = static_cast<int>(id);
  if (stack->contains(idValue)) {
    Q_ASSERT_X(false, "EventCatalog", "follow-up cycle");
    return kMaxFollowUpDepth + 1;
  }

  const auto nodeIndex = index.idToNodeIndex.constFind(idValue);
  Q_ASSERT_X(nodeIndex != index.idToNodeIndex.cend(), "EventCatalog", "follow-up id is missing");
  if (nodeIndex == index.idToNodeIndex.cend()) return kMaxFollowUpDepth + 1;

  const Node& node = catalogNodes().at(static_cast<std::size_t>(nodeIndex.value()));
  stack->insert(idValue);
  int deepest = 0;
  for (int childIndex = 0; childIndex < node.childCount; ++childIndex) {
    const int childDepth =
        followUpDepthFrom(index, node.children[static_cast<std::size_t>(childIndex)], stack);
    deepest = std::max(deepest, 1 + childDepth);
  }
  stack->remove(idValue);
  return deepest;
}

CatalogIndex buildCatalogIndex() {
  CatalogIndex index;
  const std::array<Node, 66>& nodes = catalogNodes();
  index.idToNodeIndex.reserve(static_cast<int>(nodes.size()));
  index.spanishByCanonicalName.reserve(static_cast<int>(nodes.size()));

  QHash<QString, QString> displayLabelByName;

  for (int nodeIndex = 0; nodeIndex < static_cast<int>(nodes.size()); ++nodeIndex) {
    const Node& node = nodes.at(static_cast<std::size_t>(nodeIndex));
    Q_ASSERT_X(node.canonicalName && node.canonicalName[0] != '\0', "EventCatalog",
               "catalog row is missing a canonical name");
    Q_ASSERT_X(node.id != EventId::None, "EventCatalog", "catalog row uses the empty id");
    Q_ASSERT_X(node.childCount >= 0 && node.childCount <= kMaxFollowUpChildren, "EventCatalog",
               "follow-up child count is out of range");

    const int idValue = static_cast<int>(node.id);
    Q_ASSERT_X(!index.idToNodeIndex.contains(idValue), "EventCatalog", "duplicate event id");
    index.idToNodeIndex.insert(idValue, nodeIndex);

    const QString name = QString::fromLatin1(node.canonicalName);
    const QString spanish = node.spanish ? QString::fromUtf8(node.spanish) : QString();
    const auto existingSpanish = index.spanishByCanonicalName.constFind(name);
    if (existingSpanish == index.spanishByCanonicalName.cend()) {
      index.spanishByCanonicalName.insert(name, spanish);
    } else {
      Q_ASSERT_X(*existingSpanish == spanish, "EventCatalog",
                 "one canonical name has two Spanish translations");
    }

    const bool hasDisplayLabel = node.displayLabel && node.displayLabel[0] != '\0';
    const QString displayLabel = hasDisplayLabel ? QString::fromUtf8(node.displayLabel) : QString();
    const auto existingDisplay = displayLabelByName.constFind(name);
    if (hasDisplayLabel) {
      if (existingDisplay == displayLabelByName.cend()) {
        displayLabelByName.insert(name, displayLabel);
      } else {
        Q_ASSERT_X(*existingDisplay == displayLabel, "EventCatalog",
                   "one canonical name has two display labels");
      }
    } else if (existingDisplay != displayLabelByName.cend()) {
      Q_ASSERT_X(false, "EventCatalog", "display label disagrees across rows that share a name");
    }

    if (!node.mainEvent) continue;

    Q_ASSERT_X(!index.mainNameToNodeIndex.contains(name), "EventCatalog",
               "duplicate main event name");
    Q_ASSERT_X(node.xmlCode && node.xmlCode[0] != '\0', "EventCatalog",
               "main event is missing an XML code");
    index.mainNameToNodeIndex.insert(name, nodeIndex);
  }

  for (int nodeIndex = 0; nodeIndex < static_cast<int>(nodes.size()); ++nodeIndex) {
    const Node& node = nodes.at(static_cast<std::size_t>(nodeIndex));
    QSet<QString> siblingNames;
    for (int childIndex = 0; childIndex < node.childCount; ++childIndex) {
      const int childId = static_cast<int>(node.children[static_cast<std::size_t>(childIndex)]);
      const auto childNodeIndex = index.idToNodeIndex.constFind(childId);
      Q_ASSERT_X(childNodeIndex != index.idToNodeIndex.cend(), "EventCatalog",
                 "follow-up child id is not in the catalog");
      if (childNodeIndex == index.idToNodeIndex.cend()) continue;
      const QString childName = QString::fromLatin1(
          nodes.at(static_cast<std::size_t>(childNodeIndex.value())).canonicalName);
      Q_ASSERT_X(!siblingNames.contains(childName), "EventCatalog",
                 "a follow-up row repeats a child name");
      siblingNames.insert(childName);
    }
  }

  for (auto iterator = index.mainNameToNodeIndex.cbegin();
       iterator != index.mainNameToNodeIndex.cend(); ++iterator) {
    const Node& node = nodes.at(static_cast<std::size_t>(iterator.value()));
    QSet<int> stack;
    const int depth = followUpDepthFrom(index, node.id, &stack);
    Q_ASSERT_X(depth <= kMaxFollowUpDepth, "EventCatalog", "follow-up chain is deeper than 3");
  }

  return index;
}

const CatalogIndex& catalogIndex() {
  static const CatalogIndex index = buildCatalogIndex();
  return index;
}

}  // namespace

int nodeCount() {
  return static_cast<int>(catalogNodes().size());
}

const Node& nodeAt(int index) {
  const std::array<Node, 66>& nodes = catalogNodes();
  Q_ASSERT(index >= 0 && index < static_cast<int>(nodes.size()));
  return nodes.at(static_cast<std::size_t>(index));
}

const Node* nodeForId(EventId id) {
  const CatalogIndex& index = catalogIndex();
  const auto iterator = index.idToNodeIndex.constFind(static_cast<int>(id));
  if (iterator == index.idToNodeIndex.cend()) return nullptr;
  return &nodeAt(iterator.value());
}

const Node* mainEventForCanonicalName(const QString& canonicalName) {
  const CatalogIndex& index = catalogIndex();
  const auto iterator = index.mainNameToNodeIndex.constFind(canonicalName);
  if (iterator == index.mainNameToNodeIndex.cend()) return nullptr;
  return &nodeAt(iterator.value());
}

bool containsCanonicalName(const QString& canonicalName) {
  return catalogIndex().spanishByCanonicalName.contains(canonicalName);
}

QString canonicalName(EventId id) {
  const Node* node = nodeForId(id);
  if (!node || !node->canonicalName) return {};
  return QString::fromLatin1(node->canonicalName);
}

Qt::Key shortcutKey(EventId id) {
  const Node* node = nodeForId(id);
  if (!node) return Qt::Key_unknown;
  return node->shortcut;
}

QString shortcutLabel(EventId id) {
  const int key = static_cast<int>(shortcutKey(id));
  if (key < Qt::Key_A || key > Qt::Key_Z) return {};
  return QString(QLatin1Char(static_cast<char>(key)));
}

QString spanishForCanonicalName(const QString& canonicalName) {
  const CatalogIndex& index = catalogIndex();
  const auto iterator = index.spanishByCanonicalName.constFind(canonicalName);
  if (iterator == index.spanishByCanonicalName.cend()) return {};
  return iterator.value();
}

QString displayLabelForToken(const QString& token) {
  const QString key = token.trimmed();
  if (key.isEmpty()) return {};
  const int count = nodeCount();
  for (int index = 0; index < count; ++index) {
    const Node& node = nodeAt(index);
    if (!node.displayLabel || node.displayLabel[0] == '\0' || !node.canonicalName) continue;
    if (key.compare(QLatin1String(node.canonicalName), Qt::CaseInsensitive) == 0) {
      return QString::fromUtf8(node.displayLabel);
    }
  }
  return {};
}

PayloadLayout payloadLayoutForMainEvent(const QString& canonicalName) {
  const Node* node = mainEventForCanonicalName(canonicalName);
  if (!node) return PayloadLayout::PlainChain;
  return node->payloadLayout;
}

bool switchesTeamForMainEvent(const QString& canonicalName) {
  const Node* node = mainEventForCanonicalName(canonicalName);
  return node && node->switchesTeam;
}

QStringList childCanonicalNames(const Node& node) {
  QStringList names;
  names.reserve(node.childCount);
  for (int index = 0; index < node.childCount; ++index) {
    const Node* child = nodeForId(node.children[static_cast<std::size_t>(index)]);
    if (!child || !child->canonicalName) continue;
    names.append(QString::fromLatin1(child->canonicalName));
  }
  return names;
}

const Node* childForCanonicalName(const Node& parent, const QString& canonicalName) {
  for (int index = 0; index < parent.childCount; ++index) {
    const Node* child = nodeForId(parent.children[static_cast<std::size_t>(index)]);
    if (!child || !child->canonicalName) continue;
    if (canonicalName == QLatin1String(child->canonicalName)) return child;
  }
  return nullptr;
}

}  // namespace EventCatalog
