#include "EventCatalog.h"

#include <QHash>
#include <QMetaType>
#include <QSet>
#include <QVector>

#include <algorithm>
#include <array>
#include <cstddef>

namespace {
[[maybe_unused]] const int kEventIdMetaTypeId = qRegisterMetaType<EventCatalog::EventId>();
}  // namespace

namespace EventCatalog {
namespace {

struct Behavior {
  bool scoresGoal = false;
  bool continuesAsGoal = false;
  bool isShootout = false;
  ShootoutAttemptResult shootoutAttemptResult = ShootoutAttemptResult::None;
};

constexpr Behavior goalScores() {
  Behavior behavior;
  behavior.scoresGoal = true;
  return behavior;
}

constexpr Behavior goalContinuation() {
  Behavior behavior;
  behavior.scoresGoal = true;
  behavior.continuesAsGoal = true;
  return behavior;
}

constexpr Behavior shootoutEvent() {
  Behavior behavior;
  behavior.isShootout = true;
  return behavior;
}

constexpr Behavior shootoutResult(ShootoutAttemptResult result) {
  Behavior behavior;
  behavior.shootoutAttemptResult = result;
  return behavior;
}

constexpr void applyBehavior(Node* node, Behavior behavior) {
  node->scoresGoal = behavior.scoresGoal;
  node->continuesAsGoal = behavior.continuesAsGoal;
  node->isShootout = behavior.isShootout;
  node->shootoutAttemptResult = behavior.shootoutAttemptResult;
}

template <typename... ChildIds>
constexpr Node makeFollowUpNode(EventId id, const char* canonicalName, const char* spanish,
                                Behavior behavior, ChildIds... childIds) {
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
  node.buttonRow = kNoButtonGridPosition;
  node.buttonColumn = kNoButtonGridPosition;
  node.buttonRowOffsetUnits = 0;
  applyBehavior(&node, behavior);
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
constexpr Node makeFollowUp(EventId id, const char* canonicalName, const char* spanish,
                            ChildIds... childIds) {
  return makeFollowUpNode(id, canonicalName, spanish, Behavior{}, childIds...);
}

template <typename... ChildIds>
constexpr Node makeFollowUp(EventId id, const char* canonicalName, const char* spanish,
                            Behavior behavior, ChildIds... childIds) {
  return makeFollowUpNode(id, canonicalName, spanish, behavior, childIds...);
}

template <typename... ChildIds>
constexpr Node makeMainEventNode(EventId id, const char* canonicalName, Qt::Key shortcut,
                                 const char* xmlCode, qint64 leadMs, qint64 lagMs,
                                 const char* spanish, PayloadLayout payloadLayout, bool switchesTeam,
                                 const char* displayLabel, int buttonRow, int buttonColumn,
                                 int buttonRowOffsetUnits, Behavior behavior, ChildIds... childIds) {
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
  node.buttonRow = buttonRow;
  node.buttonColumn = buttonColumn;
  node.buttonRowOffsetUnits = buttonRowOffsetUnits;
  applyBehavior(&node, behavior);
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
                             const char* displayLabel, int buttonRow, int buttonColumn,
                             int buttonRowOffsetUnits, ChildIds... childIds) {
  return makeMainEventNode(id, canonicalName, shortcut, xmlCode, leadMs, lagMs, spanish,
                           payloadLayout, switchesTeam, displayLabel, buttonRow, buttonColumn,
                           buttonRowOffsetUnits, Behavior{}, childIds...);
}

template <typename... ChildIds>
constexpr Node makeMainEvent(EventId id, const char* canonicalName, Qt::Key shortcut,
                             const char* xmlCode, qint64 leadMs, qint64 lagMs,
                             const char* spanish, PayloadLayout payloadLayout, bool switchesTeam,
                             const char* displayLabel, int buttonRow, int buttonColumn,
                             int buttonRowOffsetUnits, Behavior behavior, ChildIds... childIds) {
  return makeMainEventNode(id, canonicalName, shortcut, xmlCode, leadMs, lagMs, spanish,
                           payloadLayout, switchesTeam, displayLabel, buttonRow, buttonColumn,
                           buttonRowOffsetUnits, behavior, childIds...);
}

// Column order for makeMainEvent: id, canonical name, key, XML code, lead ms,
// lag ms, Spanish (nullptr = same as the canonical name), payload layout,
// switches team, display label (nullptr = canonical name), button row,
// button column, row left offset in quarter-key units, then children.
// Main events stay in clip-duration settings order. QSettings groups use
// canonicalName, not the id. The button row is the GameControls keyboard,
// which is not this array order:
//   row 0 offset 0: 16-yd, 50-yd, 75-yd, PC
//   row 1 offset 1: Circle Entry, PC Foul, Shot, Goal
//   row 2 offset 3: Pass, Special, Turnover, Card
//   row 3 offset 3: PS, S.O.
const std::array<Node, 66>& catalogNodes() {
  static const std::array<Node, 66> nodes = {
      makeMainEvent(EventId::Goal, "Goal", Qt::Key_F, "GOAL", 10000, 2000, "Gol",
                    PayloadLayout::TeamOnly, false, nullptr, 1, 3, 1, goalScores()),
      makeMainEvent(EventId::Shot, "Shot", Qt::Key_D, "SHOT", 5000, 5000, "Tiro",
                    PayloadLayout::PlainChain, false, nullptr, 1, 2, 1, EventId::OnTarget,
                    EventId::OffTarget, EventId::Blocked),
      makeMainEvent(EventId::PenaltyCorner, "PC", Qt::Key_R, "PC", 2000, 8000, "Corto",
                    PayloadLayout::TeamThenChain, false, nullptr, 0, 3, 0, EventId::DirectShot,
                    EventId::Variant, EventId::Ruined),
      makeMainEvent(EventId::PenaltyCornerFoul, "PC Foul", Qt::Key_S, "PCF", 5000, 2000,
                    "Falta PC", PayloadLayout::TeamThenChain, false, nullptr, 1, 1, 1,
                    EventId::Foot, EventId::Stick, EventId::Danger, EventId::Other),
      makeMainEvent(EventId::Card, "Card", Qt::Key_V, "CARD", 6000, 3000, "Tarjeta",
                    PayloadLayout::ChainThenTeam, false, nullptr, 2, 3, 3, EventId::Green,
                    EventId::Yellow, EventId::Red),
      makeMainEvent(EventId::Pass, "Pass", Qt::Key_Z, "PASS", 3000, 5000, "Pase",
                    PayloadLayout::PlainChain, false, nullptr, 2, 0, 3, EventId::Flick,
                    EventId::Push, EventId::Sweep, EventId::PassHit),
      makeMainEvent(EventId::CircleEntry, "Circle Entry", Qt::Key_A, "ENTRY", 8000, 6000,
                    "Ingreso área", PayloadLayout::TeamThenChain, false, nullptr, 1, 0, 1,
                    EventId::Dribbling, EventId::CircleEntryPass, EventId::Deflection),
      makeMainEvent(EventId::SixteenYard, "16-yd", Qt::Key_Q, "16YD", 3000, 10000, nullptr,
                    PayloadLayout::TeamThenChain, false, nullptr, 0, 0, 0, EventId::ThreeMan,
                    EventId::FourMan, EventId::Fast),
      makeMainEvent(EventId::FiftyYard, "50-yd", Qt::Key_W, "50YD", 3000, 8000, nullptr,
                    PayloadLayout::TeamOnly, false, nullptr, 0, 1, 0),
      makeMainEvent(EventId::SeventyFiveYard, "75-yd", Qt::Key_E, "75YD", 3000, 8000, nullptr,
                    PayloadLayout::ChainThenTeam, false, nullptr, 0, 2, 0, EventId::Forward,
                    EventId::Sideways, EventId::Back),
      makeMainEvent(EventId::Turnover, "Turnover", Qt::Key_C, "TO", 4000, 4000, "Pérdida",
                    PayloadLayout::PlainChain, true, nullptr, 2, 2, 3, EventId::Interception,
                    EventId::Tackle, EventId::Pressure, EventId::UnforcedError),
      makeMainEvent(EventId::Special, "Special", Qt::Key_X, "SPC", 4000, 4000, nullptr,
                    PayloadLayout::PlainChain, false, "☆", 2, 1, 3, EventId::Good, EventId::Bad,
                    EventId::Neutral, EventId::Referee),
      makeMainEvent(EventId::PenaltyStroke, "PS", Qt::Key_B, "PS", 3000, 4000, "Penal",
                    PayloadLayout::TeamThenChain, false, nullptr, 3, 0, 3, EventId::FollowUpGoal,
                    EventId::NoGoal),
      makeMainEvent(EventId::Shootout, "S.O.", Qt::Key_N, "SO", 2000, 9000, nullptr,
                    PayloadLayout::TeamThenChain, false, nullptr, 3, 1, 3, shootoutEvent(),
                    EventId::Converted, EventId::Missed, EventId::Replay),

      makeFollowUp(EventId::OnTarget, "On target", "Al arco", EventId::FollowUpGoal, EventId::Saved,
                   EventId::Post),
      makeFollowUp(EventId::OffTarget, "Off target", "Afuera", EventId::Closeby, EventId::NotClose),
      makeFollowUp(EventId::Blocked, "Blocked", "Bloqueado"),
      makeFollowUp(EventId::Saved, "Saved", "Atajado"),
      makeFollowUp(EventId::Post, "Post", "Poste"),
      makeFollowUp(EventId::Closeby, "Closeby", "Cerca"),
      makeFollowUp(EventId::NotClose, "Not close", "Lejos"),
      makeFollowUp(EventId::FollowUpGoal, "Goal", "Gol", goalContinuation()),
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
      makeFollowUp(EventId::Converted, "Converted", "Convertido",
                   shootoutResult(ShootoutAttemptResult::Converted)),
      makeFollowUp(EventId::Missed, "Missed", "No convertido",
                   shootoutResult(ShootoutAttemptResult::Missed)),
      makeFollowUp(EventId::Replay, "Replay", "Repite",
                   shootoutResult(ShootoutAttemptResult::Replay)),
  };
  return nodes;
}

struct CatalogIndex {
  QHash<int, int> idToNodeIndex;
  QHash<QString, int> mainNameToNodeIndex;
  QHash<QString, QString> spanishByCanonicalName;
  QVector<int> buttonGridNodeIndexes;
  QVector<ButtonGridRow> buttonGridRows;
  int scoringGoalNodeIndex = -1;
};

struct PlacedButton {
  int nodeIndex = 0;
  int row = 0;
  int column = 0;
  int leftOffsetUnits = 0;
};

void appendButtonGrid(const std::array<Node, 66>& nodes, CatalogIndex* index) {
  QVector<PlacedButton> placed;
  placed.reserve(static_cast<int>(nodes.size()));
  for (int nodeIndex = 0; nodeIndex < static_cast<int>(nodes.size()); ++nodeIndex) {
    const Node& node = nodes.at(static_cast<std::size_t>(nodeIndex));
    if (!node.mainEvent) {
      Q_ASSERT_X(node.buttonRow == kNoButtonGridPosition &&
                     node.buttonColumn == kNoButtonGridPosition && node.buttonRowOffsetUnits == 0,
                 "EventCatalog", "follow-up is on the button grid");
      continue;
    }
    Q_ASSERT_X(node.buttonRow >= 0, "EventCatalog", "main event is missing a button row");
    Q_ASSERT_X(node.buttonColumn >= 0 && node.buttonColumn < kButtonGridColumnCount, "EventCatalog",
               "main event button column is out of range");
    Q_ASSERT_X(node.buttonRowOffsetUnits >= 0, "EventCatalog", "button row offset is negative");
    placed.append(
        PlacedButton{nodeIndex, node.buttonRow, node.buttonColumn, node.buttonRowOffsetUnits});
  }

  std::sort(placed.begin(), placed.end(), [](const PlacedButton& left, const PlacedButton& right) {
    if (left.row != right.row) return left.row < right.row;
    return left.column < right.column;
  });

  index->buttonGridNodeIndexes.reserve(placed.size());
  int expectedRow = 0;
  int expectedColumn = 0;
  int rowOffsetUnits = 0;
  int rowStart = 0;
  for (int placedIndex = 0; placedIndex < placed.size(); ++placedIndex) {
    const PlacedButton& item = placed.at(placedIndex);
    Q_ASSERT_X(item.row == expectedRow && item.column == expectedColumn, "EventCatalog",
               "button grid skips a row or column");
    if (expectedColumn == 0) {
      rowOffsetUnits = item.leftOffsetUnits;
      rowStart = index->buttonGridNodeIndexes.size();
    } else {
      Q_ASSERT_X(item.leftOffsetUnits == rowOffsetUnits, "EventCatalog",
                 "one keyboard row has two left offsets");
    }
    index->buttonGridNodeIndexes.append(item.nodeIndex);
    ++expectedColumn;

    const bool last = placedIndex + 1 == placed.size();
    const bool nextRow = !last && placed.at(placedIndex + 1).row != item.row;
    if (expectedColumn == kButtonGridColumnCount || nextRow || last) {
      if (nextRow) {
        Q_ASSERT_X(placed.at(placedIndex + 1).row == expectedRow + 1, "EventCatalog",
                   "button grid skips a row");
      }
      ButtonGridRow row;
      row.leftOffsetUnits = rowOffsetUnits;
      row.firstEventIndex = rowStart;
      row.eventCount = expectedColumn;
      index->buttonGridRows.append(row);
      expectedColumn = 0;
      ++expectedRow;
    }
  }
}

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

    if (node.continuesAsGoal) {
      Q_ASSERT_X(!node.mainEvent, "EventCatalog", "a main event continues as a goal");
    }
    if (node.shootoutAttemptResult != ShootoutAttemptResult::None) {
      Q_ASSERT_X(!node.mainEvent, "EventCatalog", "a main event stores a shoot-out result");
      Q_ASSERT_X(!node.scoresGoal, "EventCatalog", "a shoot-out result scores a match goal");
      Q_ASSERT_X(!node.continuesAsGoal, "EventCatalog", "a shoot-out result continues as a goal");
    }
    if (node.isShootout) {
      Q_ASSERT_X(node.mainEvent, "EventCatalog", "a follow-up is marked as a shoot-out");
      Q_ASSERT_X(!node.scoresGoal, "EventCatalog", "a shoot-out scores a match goal");
      Q_ASSERT_X(!node.continuesAsGoal, "EventCatalog", "a shoot-out continues as a goal");
    }

    if (!node.mainEvent) continue;

    Q_ASSERT_X(!index.mainNameToNodeIndex.contains(name), "EventCatalog",
               "duplicate main event name");
    Q_ASSERT_X(node.xmlCode && node.xmlCode[0] != '\0', "EventCatalog",
               "main event is missing an XML code");
    index.mainNameToNodeIndex.insert(name, nodeIndex);
    if (node.scoresGoal) {
      Q_ASSERT_X(index.scoringGoalNodeIndex < 0, "EventCatalog", "two main events score a goal");
      index.scoringGoalNodeIndex = nodeIndex;
    }
  }

  Q_ASSERT_X(index.scoringGoalNodeIndex >= 0, "EventCatalog", "no main event scores a goal");

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

  appendButtonGrid(nodes, &index);
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

QString labelForId(EventId id, bool spanish) {
  const Node* node = nodeForId(id);
  if (!node || !node->canonicalName) return {};
  if (node->displayLabel && node->displayLabel[0] != '\0') {
    return QString::fromUtf8(node->displayLabel);
  }
  if (spanish && node->spanish && node->spanish[0] != '\0') {
    return QString::fromUtf8(node->spanish);
  }
  return QString::fromLatin1(node->canonicalName);
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

bool scoresGoal(EventId id) {
  const Node* node = nodeForId(id);
  return node && node->scoresGoal;
}

bool continuesAsGoal(EventId id) {
  const Node* node = nodeForId(id);
  return node && node->continuesAsGoal;
}

bool isShootout(EventId id) {
  const Node* node = nodeForId(id);
  return node && node->isShootout;
}

ShootoutAttemptResult shootoutAttemptResult(EventId id) {
  const Node* node = nodeForId(id);
  if (!node) return ShootoutAttemptResult::None;
  return node->shootoutAttemptResult;
}

bool mainEventScoresGoal(EventId id) {
  const Node* node = nodeForId(id);
  return node && node->mainEvent && node->scoresGoal;
}

const Node* scoringGoalMainEvent() {
  const int nodeIndex = catalogIndex().scoringGoalNodeIndex;
  if (nodeIndex < 0) return nullptr;
  return &nodeAt(nodeIndex);
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

int buttonGridCount() {
  return catalogIndex().buttonGridNodeIndexes.size();
}

const Node& buttonGridNodeAt(int index) {
  const QVector<int>& nodeIndexes = catalogIndex().buttonGridNodeIndexes;
  Q_ASSERT(index >= 0 && index < nodeIndexes.size());
  return nodeAt(nodeIndexes.at(index));
}

int buttonGridRowCount() {
  return catalogIndex().buttonGridRows.size();
}

ButtonGridRow buttonGridRowAt(int rowIndex) {
  const QVector<ButtonGridRow>& rows = catalogIndex().buttonGridRows;
  Q_ASSERT(rowIndex >= 0 && rowIndex < rows.size());
  return rows.at(rowIndex);
}

}  // namespace EventCatalog
