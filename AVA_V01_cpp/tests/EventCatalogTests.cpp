#include "AppLocale.h"
#include "EventCatalog.h"
#include "EventCodeMap.h"
#include "EventDefaults.h"
#include "ExportClipBuilder.h"
#include "FollowUpCatalog.h"
#include "GameControls.h"
#include "ShootoutState.h"
#include "TagEvent.h"
#include "TagSession.h"
#include "XmlExporter.h"

#include <QDate>
#include <QHBoxLayout>
#include <QLayout>
#include <QPushButton>
#include <QSet>
#include <QSignalSpy>
#include <QSizePolicy>
#include <QTest>
#include <QVector>

#include <algorithm>
#include <array>
#include <optional>

namespace {

struct ExpectedMainEvent {
  EventCatalog::EventId id;
  const char* name;
  Qt::Key key;
  const char* xmlCode;
  qint64 leadMs;
  qint64 lagMs;
  EventCatalog::PayloadLayout payloadLayout;
  bool switchesTeam;
};

// Clip-duration order. These names are the QSettings group keys.
const std::array<ExpectedMainEvent, 14>& expectedMainEvents() {
  static const std::array<ExpectedMainEvent, 14> events{{
      {EventCatalog::EventId::Goal, "Goal", Qt::Key_F, "GOAL", 10000, 2000,
       EventCatalog::PayloadLayout::TeamOnly, false},
      {EventCatalog::EventId::Shot, "Shot", Qt::Key_D, "SHOT", 5000, 5000,
       EventCatalog::PayloadLayout::PlainChain, false},
      {EventCatalog::EventId::PenaltyCorner, "PC", Qt::Key_R, "PC", 2000, 8000,
       EventCatalog::PayloadLayout::TeamThenChain, false},
      {EventCatalog::EventId::PenaltyCornerFoul, "PC Foul", Qt::Key_S, "PCF", 5000, 2000,
       EventCatalog::PayloadLayout::TeamThenChain, false},
      {EventCatalog::EventId::Card, "Card", Qt::Key_V, "CARD", 6000, 3000,
       EventCatalog::PayloadLayout::ChainThenTeam, false},
      {EventCatalog::EventId::Pass, "Pass", Qt::Key_Z, "PASS", 3000, 5000,
       EventCatalog::PayloadLayout::PlainChain, false},
      {EventCatalog::EventId::CircleEntry, "Circle Entry", Qt::Key_A, "ENTRY", 8000, 6000,
       EventCatalog::PayloadLayout::TeamThenChain, false},
      {EventCatalog::EventId::SixteenYard, "16-yd", Qt::Key_Q, "16YD", 3000, 10000,
       EventCatalog::PayloadLayout::TeamThenChain, false},
      {EventCatalog::EventId::FiftyYard, "50-yd", Qt::Key_W, "50YD", 3000, 8000,
       EventCatalog::PayloadLayout::TeamOnly, false},
      {EventCatalog::EventId::SeventyFiveYard, "75-yd", Qt::Key_E, "75YD", 3000, 8000,
       EventCatalog::PayloadLayout::ChainThenTeam, false},
      {EventCatalog::EventId::Turnover, "Turnover", Qt::Key_C, "TO", 4000, 4000,
       EventCatalog::PayloadLayout::PlainChain, true},
      {EventCatalog::EventId::Special, "Special", Qt::Key_X, "SPC", 4000, 4000,
       EventCatalog::PayloadLayout::PlainChain, false},
      {EventCatalog::EventId::PenaltyStroke, "PS", Qt::Key_B, "PS", 3000, 4000,
       EventCatalog::PayloadLayout::TeamThenChain, false},
      {EventCatalog::EventId::Shootout, "S.O.", Qt::Key_N, "SO", 2000, 9000,
       EventCatalog::PayloadLayout::TeamThenChain, false},
  }};
  return events;
}

struct ExpectedGridButton {
  EventCatalog::EventId id;
  const char* name;
  Qt::Key key;
  int row;
  int column;
  int leftOffsetUnits;
};

// The hand-laid QWERTY block: row, then column. Offsets are quarter-key units.
const std::array<ExpectedGridButton, 14>& expectedButtonGrid() {
  static const std::array<ExpectedGridButton, 14> buttons{{
      {EventCatalog::EventId::SixteenYard, "16-yd", Qt::Key_Q, 0, 0, 0},
      {EventCatalog::EventId::FiftyYard, "50-yd", Qt::Key_W, 0, 1, 0},
      {EventCatalog::EventId::SeventyFiveYard, "75-yd", Qt::Key_E, 0, 2, 0},
      {EventCatalog::EventId::PenaltyCorner, "PC", Qt::Key_R, 0, 3, 0},
      {EventCatalog::EventId::CircleEntry, "Circle Entry", Qt::Key_A, 1, 0, 1},
      {EventCatalog::EventId::PenaltyCornerFoul, "PC Foul", Qt::Key_S, 1, 1, 1},
      {EventCatalog::EventId::Shot, "Shot", Qt::Key_D, 1, 2, 1},
      {EventCatalog::EventId::Goal, "Goal", Qt::Key_F, 1, 3, 1},
      {EventCatalog::EventId::Pass, "Pass", Qt::Key_Z, 2, 0, 3},
      {EventCatalog::EventId::Special, "Special", Qt::Key_X, 2, 1, 3},
      {EventCatalog::EventId::Turnover, "Turnover", Qt::Key_C, 2, 2, 3},
      {EventCatalog::EventId::Card, "Card", Qt::Key_V, 2, 3, 3},
      {EventCatalog::EventId::PenaltyStroke, "PS", Qt::Key_B, 3, 0, 3},
      {EventCatalog::EventId::Shootout, "S.O.", Qt::Key_N, 3, 1, 3},
  }};
  return buttons;
}

struct ObservedKeyboardRow {
  int leftOffsetUnits = 0;
  QStringList names;
  QList<QPushButton*> buttons;
};

std::optional<ObservedKeyboardRow> keyboardRowFromLayout(QLayout* layout) {
  auto* rowLayout = qobject_cast<QHBoxLayout*>(layout);
  if (!rowLayout) return std::nullopt;
  ObservedKeyboardRow row;
  row.leftOffsetUnits = rowLayout->stretch(0);
  for (int index = 0; index < rowLayout->count(); ++index) {
    QLayoutItem* item = rowLayout->itemAt(index);
    if (!item) continue;
    auto* button = qobject_cast<QPushButton*>(item->widget());
    if (!button) continue;
    const QString name = button->property("gameEventName").toString();
    if (name.isEmpty()) continue;
    row.names.append(name);
    row.buttons.append(button);
  }
  if (row.names.isEmpty()) return std::nullopt;
  return row;
}

void collectKeyboardRows(QWidget* widget, QList<ObservedKeyboardRow>* rows) {
  if (!widget || !widget->layout()) return;
  if (std::optional<ObservedKeyboardRow> row = keyboardRowFromLayout(widget->layout())) {
    rows->append(*row);
    return;
  }
  QLayout* layout = widget->layout();
  for (int index = 0; index < layout->count(); ++index) {
    QLayoutItem* item = layout->itemAt(index);
    if (!item) continue;
    if (QWidget* child = item->widget()) collectKeyboardRows(child, rows);
  }
}

QStringList names(std::initializer_list<const char*> values) {
  QStringList result;
  result.reserve(static_cast<int>(values.size()));
  for (const char* value : values) result.append(QString::fromUtf8(value));
  return result;
}

struct ExpectedTranslation {
  const char* token;
  const char* english;
  const char* spanish;
};

const std::array<ExpectedTranslation, 69>& expectedTranslations() {
  static const std::array<ExpectedTranslation, 69> translations{{
      {"Circle Entry", "Circle Entry", "Ingreso área"},
      {"Shot", "Shot", "Tiro"},
      {"Goal", "Goal", "Gol"},
      {"PC", "PC", "Corto"},
      {"PS", "PS", "Penal"},
      {"S.O.", "S.O.", "S.O."},
      {"Pass", "Pass", "Pase"},
      {"Turnover", "Turnover", "Pérdida"},
      {"Card", "Card", "Tarjeta"},
      {"PC Foul", "PC Foul", "Falta PC"},
      {"16-yd", "16-yd", "16-yd"},
      {"50-yd", "50-yd", "50-yd"},
      {"75-yd", "75-yd", "75-yd"},
      {"Special", "☆", "☆"},
      {"On target", "On target", "Al arco"},
      {"Off target", "Off target", "Afuera"},
      {"Blocked", "Blocked", "Bloqueado"},
      {"For", "For", "A favor"},
      {"Against", "Against", "En contra"},
      {"Direct shot", "Direct shot", "Directo"},
      {"Variant", "Variant", "Variante"},
      {"Ruined", "Ruined", "Arruinado"},
      {"Forward", "Forward", "Hacia adelante"},
      {"Sideways", "Sideways", "Hacia el lado"},
      {"Back", "Back", "Hacia atrás"},
      {"Green", "Green", "Verde"},
      {"Yellow", "Yellow", "Amarilla"},
      {"Red", "Red", "Roja"},
      {"Flick", "Flick", "Flick"},
      {"Push", "Push", "Push"},
      {"Sweep", "Sweep", "Sweep"},
      {"Hit", "Hit", "Pegada"},
      {"Good", "Good", "Positivo"},
      {"Bad", "Bad", "Negativo"},
      {"Neutral", "Neutral", "Neutro"},
      {"Referee", "Referee", "Arbitraje"},
      {"Off", "Off", "Ofensiva"},
      {"Def", "Def", "Defensiva"},
      {"Saved", "Saved", "Atajado"},
      {"Post", "Post", "Poste"},
      {"Closeby", "Closeby", "Cerca"},
      {"Not close", "Not close", "Lejos"},
      {"Swept", "Swept", "Sweep"},
      {"Dragflick", "Dragflick", "Arrastre"},
      {"New PC", "New PC", "Nuevo corto"},
      {"Dribbling", "Dribbling", "Conducción"},
      {"Deflection", "Deflection", "Desvío"},
      {"Completed", "Completed", "Completado"},
      {"Failed", "Failed", "Fallido"},
      {"Interception", "Interception", "Intercepción"},
      {"Tackle", "Tackle", "Quite"},
      {"Pressure", "Pressure", "Presión"},
      {"Unforced error", "Unforced error", "Error"},
      {"Foot", "Foot", "Pie"},
      {"Stick", "Stick", "Palo"},
      {"Danger", "Danger", "Peligro"},
      {"Other", "Other", "Otro"},
      {"Converted", "Converted", "Convertido"},
      {"Missed", "Missed", "No convertido"},
      {"Replay", "Replay", "Repite"},
      {"Left", "Left", "Izquierda"},
      {"Middle", "Middle", "Centro"},
      {"Right", "Right", "Derecha"},
      {"3 man", "3 man", "de 3"},
      {"4 man", "4 man", "de 4"},
      {"Fast", "Fast", "rápida"},
      {"No Goal", "No Goal", "No Goal"},
      {"home", "home", "Local"},
      {"away", "away", "Visita"},
  }};
  return translations;
}

int followUpDepth(EventCatalog::EventId id, QSet<int>* stack, QString* error) {
  const int idValue = static_cast<int>(id);
  if (stack->contains(idValue)) {
    *error = QStringLiteral("cycle at id %1").arg(idValue);
    return EventCatalog::kMaxFollowUpDepth + 1;
  }
  const EventCatalog::Node* node = EventCatalog::nodeForId(id);
  if (!node) {
    *error = QStringLiteral("missing id %1").arg(idValue);
    return EventCatalog::kMaxFollowUpDepth + 1;
  }
  stack->insert(idValue);
  int deepest = 0;
  for (int index = 0; index < node->childCount; ++index) {
    QString childError;
    const int childDepth =
        followUpDepth(node->children.at(static_cast<std::size_t>(index)), stack, &childError);
    if (!childError.isEmpty()) {
      *error = childError;
      stack->remove(idValue);
      return childDepth;
    }
    deepest = std::max(deepest, 1 + childDepth);
  }
  stack->remove(idValue);
  return deepest;
}

bool findContinuation(const EventCatalog::Node& node, QVector<EventCatalog::EventId>* path) {
  if (node.continuesAsGoal) return true;
  for (int childIndex = 0; childIndex < node.childCount; ++childIndex) {
    const EventCatalog::Node* child =
        EventCatalog::nodeForId(node.children.at(static_cast<std::size_t>(childIndex)));
    if (!child) return false;
    path->append(child->id);
    if (findContinuation(*child, path)) return true;
    path->removeLast();
  }
  return false;
}

void expectLeafContinuationMatchesFlag(const EventCatalog::Node& mainEvent,
                                       const EventCatalog::Node& node, QStringList* selections) {
  if (node.childCount == 0) {
    QCOMPARE(FollowUpCatalog::continuesAsGoal(QString::fromLatin1(mainEvent.canonicalName),
                                              *selections),
             node.continuesAsGoal);
    return;
  }
  for (int childIndex = 0; childIndex < node.childCount; ++childIndex) {
    const EventCatalog::Node* child =
        EventCatalog::nodeForId(node.children.at(static_cast<std::size_t>(childIndex)));
    QVERIFY(child != nullptr);
    if (!child) return;
    selections->append(QString::fromLatin1(child->canonicalName));
    expectLeafContinuationMatchesFlag(mainEvent, *child, selections);
    selections->removeLast();
  }
}

TagEvent::FollowUpChain chainFromIds(const QVector<EventCatalog::EventId>& ids) {
  TagEvent::FollowUpChain chain;
  chain.count = ids.size();
  for (int index = 0; index < ids.size(); ++index) {
    chain.ids[static_cast<std::size_t>(index)] = ids.at(index);
  }
  return chain;
}

TagSession::GameTag playedTag(EventCatalog::EventId id, const QString& team, qint64 markMs,
                              const TagEvent::FollowUpChain& chain = {}) {
  TagSession::GameTag tag;
  tag.setMainEventKey(TagEvent::catalogKey(id));
  tag.setFollowUpChain(chain);
  tag.team = team;
  tag.markMs = markMs;
  tag.startMs = markMs > 2000 ? markMs - 2000 : 0;
  tag.endMs = markMs + 2000;
  tag.intervalManuallyEdited = true;
  return tag;
}

void configureMatch(TagSession* session) {
  session->setGameTeams(QStringLiteral("UC"), QStringLiteral("OR"), QStringLiteral("#3c5ac8"),
                        QStringLiteral("#c83c3c"));
  session->setGameMetadata(QDate(2026, 9, 27), QStringLiteral("UC"), QStringLiteral("OR"));
}

}  // namespace

class EventCatalogTest : public QObject {
  Q_OBJECT

 private slots:
  void everyMainEventHasAnXmlCode();
  void idsAreUnique();
  void followUpDepthIsAtMostThree();
  void keyboardKeysAreUnique();
  void mainEventsMatchThePreviousTables();
  void followUpTreeMatchesThePreviousCatalog();
  void translationsMatchThePreviousTable();
  void gameControlButtonsUseCatalogShortcuts();
  void buttonGridMatchesTheHandLaidKeyboard();
  void gameControlsGridMatchesTheCatalogOrder();
  void goalPenaltyAndShootoutUseCatalogFlags();
};

void EventCatalogTest::everyMainEventHasAnXmlCode() {
  QSet<QString> xmlCodes;
  int mainEventCount = 0;
  for (int index = 0; index < EventCatalog::nodeCount(); ++index) {
    const EventCatalog::Node& node = EventCatalog::nodeAt(index);
    if (!node.mainEvent) {
      QVERIFY2(node.xmlCode == nullptr, node.canonicalName);
      continue;
    }
    ++mainEventCount;
    QVERIFY(node.xmlCode != nullptr);
    QVERIFY(node.xmlCode[0] != '\0');
    const QString code = QString::fromLatin1(node.xmlCode);
    QVERIFY2(!xmlCodes.contains(code), qPrintable(code));
    xmlCodes.insert(code);
  }
  QCOMPARE(mainEventCount, 14);
  QCOMPARE(xmlCodes.size(), 14);
}

void EventCatalogTest::idsAreUnique() {
  QSet<int> seenIds;
  for (int index = 0; index < EventCatalog::nodeCount(); ++index) {
    const EventCatalog::Node& node = EventCatalog::nodeAt(index);
    const int idValue = static_cast<int>(node.id);
    QVERIFY(idValue != static_cast<int>(EventCatalog::EventId::None));
    QVERIFY2(!seenIds.contains(idValue), qPrintable(QString::number(idValue)));
    seenIds.insert(idValue);
    QCOMPARE(EventCatalog::nodeForId(node.id), &node);
  }
  QCOMPARE(seenIds.size(), EventCatalog::nodeCount());

  const std::array<ExpectedMainEvent, 14>& expected = expectedMainEvents();
  for (int index = 0; index < static_cast<int>(expected.size()); ++index) {
    QCOMPARE(static_cast<int>(expected.at(static_cast<std::size_t>(index)).id), index + 1);
  }
  QCOMPARE(static_cast<int>(EventCatalog::EventId::OnTarget), 100);
  QCOMPARE(static_cast<int>(EventCatalog::EventId::FollowUpGoal), 107);
  QCOMPARE(static_cast<int>(EventCatalog::EventId::Replay), 151);
}

void EventCatalogTest::followUpDepthIsAtMostThree() {
  QSet<int> reachable;
  int deepest = 0;
  for (int index = 0; index < EventCatalog::nodeCount(); ++index) {
    const EventCatalog::Node& node = EventCatalog::nodeAt(index);
    QVERIFY(node.childCount >= 0);
    QVERIFY(node.childCount <= EventCatalog::kMaxFollowUpChildren);
    for (int childIndex = node.childCount; childIndex < EventCatalog::kMaxFollowUpChildren;
         ++childIndex) {
      QCOMPARE(node.children.at(static_cast<std::size_t>(childIndex)), EventCatalog::EventId::None);
    }
    for (int childIndex = 0; childIndex < node.childCount; ++childIndex) {
      const EventCatalog::EventId childId = node.children.at(static_cast<std::size_t>(childIndex));
      QVERIFY(childId != EventCatalog::EventId::None);
      QVERIFY(EventCatalog::nodeForId(childId) != nullptr);
    }
    if (!node.mainEvent) continue;

    QSet<int> stack;
    QString error;
    const int depth = followUpDepth(node.id, &stack, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY2(depth <= EventCatalog::kMaxFollowUpDepth, node.canonicalName);
    deepest = std::max(deepest, depth);

    const auto markReachable = [&](auto&& self, EventCatalog::EventId id) -> bool {
      const int idValue = static_cast<int>(id);
      if (reachable.contains(idValue)) return true;
      const EventCatalog::Node* current = EventCatalog::nodeForId(id);
      if (!current) return false;
      reachable.insert(idValue);
      for (int childIndex = 0; childIndex < current->childCount; ++childIndex) {
        if (!self(self, current->children.at(static_cast<std::size_t>(childIndex)))) return false;
      }
      return true;
    };
    QVERIFY2(markReachable(markReachable, node.id), node.canonicalName);
  }

  QCOMPARE(deepest, EventCatalog::kMaxFollowUpDepth);
  QCOMPARE(reachable.size(), EventCatalog::nodeCount());
}

void EventCatalogTest::keyboardKeysAreUnique() {
  QSet<int> seenKeys;
  const QSet<int> reservedKeys = {
      Qt::Key_G,     Qt::Key_H,     Qt::Key_Escape, Qt::Key_1, Qt::Key_2,
      Qt::Key_3,     Qt::Key_4,     Qt::Key_5,      Qt::Key_6, Qt::Key_7,
      Qt::Key_8,     Qt::Key_9,     Qt::Key_Space,
  };
  int letterKeyCount = 0;
  for (int index = 0; index < EventCatalog::nodeCount(); ++index) {
    const EventCatalog::Node& node = EventCatalog::nodeAt(index);
    if (!node.mainEvent) {
      QCOMPARE(node.shortcut, Qt::Key_unknown);
      continue;
    }
    QVERIFY(node.shortcut != Qt::Key_unknown);
    const int key = static_cast<int>(node.shortcut);
    QVERIFY2(!seenKeys.contains(key), node.canonicalName);
    QVERIFY2(!reservedKeys.contains(key), node.canonicalName);
    QVERIFY(key >= Qt::Key_A && key <= Qt::Key_Z);
    seenKeys.insert(key);
    QCOMPARE(EventCatalog::shortcutLabel(node.id), QString(QLatin1Char(static_cast<char>(key))));
    ++letterKeyCount;
  }
  QCOMPARE(letterKeyCount, 14);
  QCOMPARE(seenKeys.size(), 14);
}

void EventCatalogTest::mainEventsMatchThePreviousTables() {
  const std::array<ExpectedMainEvent, 14>& expected = expectedMainEvents();
  QStringList configurableNames;
  int mainEventIndex = 0;
  for (int index = 0; index < EventCatalog::nodeCount(); ++index) {
    const EventCatalog::Node& node = EventCatalog::nodeAt(index);
    if (!node.mainEvent) continue;
    QVERIFY(mainEventIndex < static_cast<int>(expected.size()));
    const ExpectedMainEvent& row = expected.at(static_cast<std::size_t>(mainEventIndex));
    QCOMPARE(node.id, row.id);
    QCOMPARE(QString::fromLatin1(node.canonicalName), QString::fromLatin1(row.name));
    QCOMPARE(node.shortcut, row.key);
    QCOMPARE(QString::fromLatin1(node.xmlCode), QString::fromLatin1(row.xmlCode));
    QCOMPARE(node.leadMs, row.leadMs);
    QCOMPARE(node.lagMs, row.lagMs);
    QCOMPARE(node.payloadLayout, row.payloadLayout);
    QCOMPARE(node.switchesTeam, row.switchesTeam);

    const QString name = QString::fromLatin1(row.name);
    configurableNames.append(name);
    const std::optional<QString> shortCode = EventCodeMap::shortCodeForMainEvent(name);
    QVERIFY(shortCode.has_value());
    QCOMPARE(*shortCode, QString::fromLatin1(row.xmlCode));
    const std::optional<QString> roundTrip = EventCodeMap::mainEventForShortCode(*shortCode);
    QVERIFY(roundTrip.has_value());
    QCOMPARE(*roundTrip, name);

    const EventDefaults::EventDuration duration = EventDefaults::factoryDefaultFor(name);
    QCOMPARE(duration.leadMs, row.leadMs);
    QCOMPARE(duration.lagMs, row.lagMs);
    QCOMPARE(FollowUpCatalog::payloadLayout(name), row.payloadLayout);
    QCOMPARE(FollowUpCatalog::switchesTeamOnCommit(name), row.switchesTeam);
    ++mainEventIndex;
  }
  QCOMPARE(mainEventIndex, static_cast<int>(expected.size()));
  QCOMPARE(EventDefaults::allConfigurableEventTypes(), configurableNames);

  const std::optional<QString> entryCode =
      EventCodeMap::shortCodeForMainEvent(QStringLiteral("  circle entry "));
  QVERIFY(entryCode.has_value());
  QCOMPARE(*entryCode, QStringLiteral("ENTRY"));
  const std::optional<QString> entryName = EventCodeMap::mainEventForShortCode(QStringLiteral("entry"));
  QVERIFY(entryName.has_value());
  QCOMPARE(*entryName, QStringLiteral("Circle Entry"));
  QVERIFY(!EventCodeMap::shortCodeForMainEvent(QStringLiteral("On target")).has_value());

  QCOMPARE(EventDefaults::factoryDefaultFor(QStringLiteral("On target")).leadMs, qint64{3000});
  QCOMPARE(EventDefaults::factoryDefaultFor(QStringLiteral("On target")).lagMs, qint64{4000});
  QCOMPARE(EventDefaults::factoryDefaultFor(QStringLiteral("Inicio")).leadMs, qint64{0});
  QCOMPARE(EventDefaults::factoryDefaultFor(QStringLiteral("Inicio")).lagMs, qint64{2000});
  QCOMPARE(EventDefaults::factoryDefaultFor(QStringLiteral("TM")).leadMs, qint64{0});
  QCOMPARE(EventDefaults::factoryDefaultFor(QStringLiteral("TM")).lagMs, qint64{0});
  QVERIFY(EventDefaults::isTimeControlEvent(QStringLiteral("Inicio")));
  QVERIFY(EventDefaults::isTimeControlEvent(QStringLiteral("Q2")));
  QVERIFY(!EventDefaults::isTimeControlEvent(QStringLiteral("Goal")));
  QVERIFY(EventDefaults::isQuarterEvent(QStringLiteral("Q1")));
  QVERIFY(EventDefaults::isQuarterEvent(QStringLiteral("Q4")));
  QVERIFY(!EventDefaults::isQuarterEvent(QStringLiteral("Inicio")));
  QCOMPARE(EventDefaults::quarterCode(0), QStringLiteral("Q1"));
  QCOMPARE(EventDefaults::quarterCode(3), QStringLiteral("Q4"));
  QCOMPARE(EventDefaults::quarterCode(4), QString());
  QVERIFY(!configurableNames.contains(QStringLiteral("Q1")));
  QVERIFY(!configurableNames.contains(QStringLiteral("Inicio")));
}

void EventCatalogTest::followUpTreeMatchesThePreviousCatalog() {
  QCOMPARE(FollowUpCatalog::firstLevelOptions(QStringLiteral("Shot")),
           names({"On target", "Off target", "Blocked"}));
  QCOMPARE(FollowUpCatalog::secondLevelOptions(QStringLiteral("Shot"), QStringLiteral("On target")),
           names({"Goal", "Saved", "Post"}));
  QCOMPARE(FollowUpCatalog::secondLevelOptions(QStringLiteral("Shot"), QStringLiteral("Off target")),
           names({"Closeby", "Not close"}));
  QCOMPARE(FollowUpCatalog::secondLevelOptions(QStringLiteral("Shot"), QStringLiteral("Blocked")),
           names({}));
  QCOMPARE(FollowUpCatalog::thirdLevelOptions(QStringLiteral("Shot"), QStringLiteral("On target"),
                                              QStringLiteral("Goal")),
           names({}));

  QCOMPARE(FollowUpCatalog::firstLevelOptions(QStringLiteral("Circle Entry")),
           names({"Dribbling", "Pass", "Deflection"}));
  for (const char* entry : {"Dribbling", "Pass", "Deflection"}) {
    QCOMPARE(FollowUpCatalog::secondLevelOptions(QStringLiteral("Circle Entry"),
                                                QString::fromUtf8(entry)),
             names({"Left", "Middle", "Right"}));
  }

  QCOMPARE(FollowUpCatalog::firstLevelOptions(QStringLiteral("PC")),
           names({"Direct shot", "Variant", "Ruined"}));
  QCOMPARE(FollowUpCatalog::secondLevelOptions(QStringLiteral("PC"), QStringLiteral("Direct shot")),
           names({"Hit", "Swept", "Dragflick"}));
  for (const char* technique : {"Hit", "Swept", "Dragflick"}) {
    QCOMPARE(FollowUpCatalog::thirdLevelOptions(QStringLiteral("PC"), QStringLiteral("Direct shot"),
                                                QString::fromUtf8(technique)),
             names({"Goal", "No Goal"}));
  }
  QCOMPARE(FollowUpCatalog::secondLevelOptions(QStringLiteral("PC"), QStringLiteral("Variant")),
           names({"Goal", "No Goal", "New PC"}));
  QCOMPARE(FollowUpCatalog::secondLevelOptions(QStringLiteral("PC"), QStringLiteral("Ruined")),
           names({"Goal", "No Goal", "New PC"}));
  QCOMPARE(FollowUpCatalog::optionsAfter(
               QStringLiteral("PC"),
               names({"Direct shot", "Hit", "Goal"})),
           names({}));

  QCOMPARE(FollowUpCatalog::firstLevelOptions(QStringLiteral("16-yd")),
           names({"3 man", "4 man", "Fast"}));
  QCOMPARE(FollowUpCatalog::secondLevelOptions(QStringLiteral("16-yd"), QStringLiteral("3 man")),
           names({}));
  QCOMPARE(FollowUpCatalog::firstLevelOptions(QStringLiteral("75-yd")),
           names({"Forward", "Sideways", "Back"}));
  QCOMPARE(FollowUpCatalog::firstLevelOptions(QStringLiteral("Card")),
           names({"Green", "Yellow", "Red"}));
  QCOMPARE(FollowUpCatalog::firstLevelOptions(QStringLiteral("Pass")),
           names({"Flick", "Push", "Sweep", "Hit"}));
  for (const char* pass : {"Flick", "Push", "Sweep", "Hit"}) {
    QCOMPARE(FollowUpCatalog::secondLevelOptions(QStringLiteral("Pass"), QString::fromUtf8(pass)),
             names({"Completed", "Failed"}));
  }
  QCOMPARE(FollowUpCatalog::secondLevelOptions(QStringLiteral("Pass"), QStringLiteral("Pass")),
           names({}));
  QCOMPARE(FollowUpCatalog::thirdLevelOptions(QStringLiteral("Pass"), QStringLiteral("Hit"),
                                              QStringLiteral("Goal")),
           names({}));

  QCOMPARE(FollowUpCatalog::firstLevelOptions(QStringLiteral("Special")),
           names({"Good", "Bad", "Neutral", "Referee"}));
  QCOMPARE(FollowUpCatalog::firstLevelOptions(QStringLiteral("Turnover")),
           names({"Interception", "Tackle", "Pressure", "Unforced error"}));
  QCOMPARE(FollowUpCatalog::firstLevelOptions(QStringLiteral("PC Foul")),
           names({"Foot", "Stick", "Danger", "Other"}));
  QCOMPARE(FollowUpCatalog::firstLevelOptions(QStringLiteral("PS")), names({"Goal", "No Goal"}));
  QCOMPARE(FollowUpCatalog::firstLevelOptions(QStringLiteral("S.O.")),
           names({"Converted", "Missed", "Replay"}));
  QCOMPARE(FollowUpCatalog::firstLevelOptions(QStringLiteral("Goal")), names({}));
  QCOMPARE(FollowUpCatalog::firstLevelOptions(QStringLiteral("50-yd")), names({}));
  QCOMPARE(FollowUpCatalog::firstLevelOptions(QStringLiteral("Not an event")), names({}));

  const QString arrow = AppLocale::kCompoundPathSeparator;
  QCOMPARE(FollowUpCatalog::formatPayload(QStringLiteral("Shot"),
                                          names({"On target", "Saved"}), QStringLiteral("Home")),
           QStringLiteral("On target") + arrow + QStringLiteral("Saved"));
  QCOMPARE(FollowUpCatalog::formatPayload(QStringLiteral("PC"), names({"Ruined", "New PC"}),
                                          QStringLiteral("Local")),
           QStringLiteral("Local") + arrow + QStringLiteral("Ruined") + arrow +
               QStringLiteral("New PC"));
  QCOMPARE(FollowUpCatalog::formatPayload(QStringLiteral("Card"), names({"Yellow"}),
                                          QStringLiteral("Visita")),
           QStringLiteral("Yellow") + arrow + QStringLiteral("Visita"));
  QCOMPARE(FollowUpCatalog::formatPayload(QStringLiteral("75-yd"), names({"Back"}),
                                          QStringLiteral("A")),
           QStringLiteral("Back") + arrow + QStringLiteral("A"));
  QCOMPARE(FollowUpCatalog::formatPayload(QStringLiteral("Goal"), names({"ignored"}),
                                          QStringLiteral("Club")),
           QStringLiteral("Club"));
  QCOMPARE(FollowUpCatalog::formatPayload(QStringLiteral("50-yd"), names({"ignored"}),
                                          QStringLiteral("Club")),
           QStringLiteral("Club"));
  QCOMPARE(FollowUpCatalog::formatPayload(QStringLiteral("Pass"), names({"Flick", "Failed"}),
                                          QString()),
           QStringLiteral("Flick") + arrow + QStringLiteral("Failed"));
  QCOMPARE(FollowUpCatalog::formatPayload(QStringLiteral("Turnover"), names({}),
                                          QStringLiteral("Home")),
           QString());

  QVERIFY(FollowUpCatalog::continuesAsGoal(QStringLiteral("PS"), names({"Goal"})));
  QVERIFY(FollowUpCatalog::continuesAsGoal(QStringLiteral("PC"),
                                           names({"Direct shot", "Hit", "Goal"})));
  QVERIFY(FollowUpCatalog::continuesAsGoal(QStringLiteral("Shot"), names({"On target", "Goal"})));
  QVERIFY(!FollowUpCatalog::continuesAsGoal(QStringLiteral("Shot"),
                                            names({"Off target", "Closeby"})));
  QVERIFY(!FollowUpCatalog::continuesAsGoal(QStringLiteral("S.O."), names({"Converted"})));
  QVERIFY(!FollowUpCatalog::continuesAsGoal(QStringLiteral("PS"), names({})));
  QVERIFY(FollowUpCatalog::switchesTeamOnCommit(QStringLiteral("Turnover")));
  QVERIFY(!FollowUpCatalog::switchesTeamOnCommit(QStringLiteral("Pass")));
}

void EventCatalogTest::translationsMatchThePreviousTable() {
  const std::array<ExpectedTranslation, 69>& translations = expectedTranslations();
  QSet<QString> seenTokens;
  for (const ExpectedTranslation& row : translations) {
    const QString token = QString::fromUtf8(row.token);
    QVERIFY2(!seenTokens.contains(token), qPrintable(token));
    seenTokens.insert(token);
    QCOMPARE(AppLocale::trEventForLanguage(token, AppLocale::Language::English),
             QString::fromUtf8(row.english));
    QCOMPARE(AppLocale::trEventForLanguage(token, AppLocale::Language::Spanish),
             QString::fromUtf8(row.spanish));
  }

  QCOMPARE(AppLocale::trEventForLanguage(QStringLiteral(" SPECIAL "), AppLocale::Language::English),
           QStringLiteral("☆"));
  QCOMPARE(AppLocale::trEventForLanguage(QStringLiteral("special"), AppLocale::Language::Spanish),
           QStringLiteral("☆"));
  QCOMPARE(AppLocale::trEventForLanguage(QString(), AppLocale::Language::Spanish), QString());
  QCOMPARE(AppLocale::trEventForLanguage(QStringLiteral("   "), AppLocale::Language::Spanish),
           QString());
  QCOMPARE(AppLocale::trEventForLanguage(QStringLiteral("NotAnEvent"), AppLocale::Language::Spanish),
           QStringLiteral("NotAnEvent"));

  for (int index = 0; index < EventCatalog::nodeCount(); ++index) {
    const EventCatalog::Node& node = EventCatalog::nodeAt(index);
    const QString token = QString::fromLatin1(node.canonicalName);
    QVERIFY2(seenTokens.contains(token), qPrintable(token));
  }
  QVERIFY(seenTokens.contains(QStringLiteral("home")));
  QVERIFY(seenTokens.contains(QStringLiteral("For")));
}

void EventCatalogTest::gameControlButtonsUseCatalogShortcuts() {
  GameControls controls;
  const std::array<ExpectedMainEvent, 14>& expected = expectedMainEvents();
  for (const ExpectedMainEvent& row : expected) {
    QPushButton* button = nullptr;
    const QString name = QString::fromLatin1(row.name);
    for (QPushButton* candidate : controls.findChildren<QPushButton*>()) {
      if (candidate->property("gameEventName").toString() == name) {
        button = candidate;
        break;
      }
    }
    QVERIFY2(button, row.name);
    QCOMPARE(button->property("shortcutHint").toString(), EventCatalog::shortcutLabel(row.id));
  }
}

void EventCatalogTest::buttonGridMatchesTheHandLaidKeyboard() {
  const std::array<ExpectedGridButton, 14>& expected = expectedButtonGrid();
  QCOMPARE(EventCatalog::buttonGridCount(), static_cast<int>(expected.size()));
  QCOMPARE(EventCatalog::buttonGridRowCount(), 4);

  QSet<int> seenIds;
  int flatIndex = 0;
  int expectedRowStart = 0;
  for (int rowIndex = 0; rowIndex < EventCatalog::buttonGridRowCount(); ++rowIndex) {
    const EventCatalog::ButtonGridRow row = EventCatalog::buttonGridRowAt(rowIndex);
    const int rowLength = rowIndex == 3 ? 2 : EventCatalog::kButtonGridColumnCount;
    QCOMPARE(row.eventCount, rowLength);
    QCOMPARE(row.firstEventIndex, expectedRowStart);
    const ExpectedGridButton& rowStart = expected.at(static_cast<std::size_t>(expectedRowStart));
    QCOMPARE(row.leftOffsetUnits, rowStart.leftOffsetUnits);
    for (int column = 0; column < row.eventCount; ++column) {
      const ExpectedGridButton& expectedButton = expected.at(static_cast<std::size_t>(flatIndex));
      QCOMPARE(expectedButton.row, rowIndex);
      QCOMPARE(expectedButton.column, column);
      const EventCatalog::Node& node = EventCatalog::buttonGridNodeAt(row.firstEventIndex + column);
      QCOMPARE(node.id, expectedButton.id);
      QCOMPARE(QString::fromLatin1(node.canonicalName), QString::fromLatin1(expectedButton.name));
      QCOMPARE(node.shortcut, expectedButton.key);
      QCOMPARE(node.buttonRow, expectedButton.row);
      QCOMPARE(node.buttonColumn, expectedButton.column);
      QCOMPARE(node.buttonRowOffsetUnits, expectedButton.leftOffsetUnits);
      QCOMPARE(EventCatalog::shortcutKey(node.id), expectedButton.key);
      QCOMPARE(EventCatalog::shortcutLabel(node.id),
               QString(QLatin1Char(static_cast<char>(expectedButton.key))));
      QVERIFY(!seenIds.contains(static_cast<int>(node.id)));
      seenIds.insert(static_cast<int>(node.id));
      QCOMPARE(flatIndex, row.firstEventIndex + column);
      ++flatIndex;
    }
    expectedRowStart += row.eventCount;
  }
  QCOMPARE(flatIndex, static_cast<int>(expected.size()));
  QCOMPARE(seenIds.size(), static_cast<int>(expected.size()));

  int mainEventCount = 0;
  for (int index = 0; index < EventCatalog::nodeCount(); ++index) {
    const EventCatalog::Node& node = EventCatalog::nodeAt(index);
    if (node.mainEvent) {
      ++mainEventCount;
      QVERIFY(seenIds.contains(static_cast<int>(node.id)));
      continue;
    }
    QCOMPARE(node.buttonRow, EventCatalog::kNoButtonGridPosition);
    QCOMPARE(node.buttonColumn, EventCatalog::kNoButtonGridPosition);
    QCOMPARE(node.buttonRowOffsetUnits, 0);
  }
  QCOMPARE(mainEventCount, static_cast<int>(expected.size()));
}

void EventCatalogTest::gameControlsGridMatchesTheCatalogOrder() {
  GameControls controls;
  controls.setTeamSide(TagSession::TeamSide::Home);
  controls.resize(420, 780);
  controls.show();

  QList<ObservedKeyboardRow> rows;
  collectKeyboardRows(&controls, &rows);
  const std::array<ExpectedGridButton, 14>& expected = expectedButtonGrid();
  QCOMPARE(rows.size(), EventCatalog::buttonGridRowCount());

  int namedButtonCount = 0;
  for (QPushButton* button : controls.findChildren<QPushButton*>()) {
    if (!button->property("gameEventName").toString().isEmpty()) ++namedButtonCount;
  }
  QCOMPARE(namedButtonCount, static_cast<int>(expected.size()));

  QSignalSpy captured(&controls, &GameControls::mainEventTimestampCaptured);
  int flatIndex = 0;
  for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
    const ObservedKeyboardRow& row = rows.at(rowIndex);
    const EventCatalog::ButtonGridRow catalogRow = EventCatalog::buttonGridRowAt(rowIndex);
    QCOMPARE(row.leftOffsetUnits, catalogRow.leftOffsetUnits);
    QCOMPARE(row.names.size(), catalogRow.eventCount);
    QCOMPARE(row.buttons.size(), catalogRow.eventCount);
    for (int column = 0; column < row.names.size(); ++column) {
      const ExpectedGridButton& expectedButton = expected.at(static_cast<std::size_t>(flatIndex));
      const EventCatalog::Node& node =
          EventCatalog::buttonGridNodeAt(catalogRow.firstEventIndex + column);
      const QString name = QString::fromLatin1(expectedButton.name);
      QCOMPARE(row.names.at(column), name);
      QCOMPARE(QString::fromLatin1(node.canonicalName), name);
      QPushButton* button = row.buttons.at(column);
      QCOMPARE(button->property("gameEventName").toString(), name);
      QCOMPARE(button->property("shortcutHint").toString(), EventCatalog::shortcutLabel(node.id));
      QCOMPARE(button->property("variant").toString(), QStringLiteral("gameControl"));
      QCOMPARE(button->focusPolicy(), Qt::ClickFocus);
      QCOMPARE(button->minimumHeight(), 52);
      QCOMPARE(button->sizePolicy().horizontalPolicy(), QSizePolicy::Expanding);
      QCOMPARE(button->sizePolicy().verticalPolicy(), QSizePolicy::Fixed);

      captured.clear();
      QTest::keyClick(&controls, expectedButton.key);
      QCOMPARE(captured.count(), 1);
      QCOMPARE(captured.at(0).at(0).toString(), name);
      QTest::keyClick(&controls, Qt::Key_Escape);
      ++flatIndex;
    }
  }
  QCOMPARE(flatIndex, static_cast<int>(expected.size()));

  captured.clear();
  QSignalSpy advanced(&controls, &GameControls::tagSequenceAdvanced);
  QSignalSpy committed(&controls, &GameControls::tagCommitted);
  QSignalSpy dismissed(&controls, &GameControls::tagSequenceDismissed);
  QTest::keyClick(&controls, Qt::Key_D);
  QCOMPARE(captured.count(), 1);
  QCOMPARE(captured.at(0).at(0).toString(), QStringLiteral("Shot"));
  QTest::keyClick(&controls, Qt::Key_1);
  QCOMPARE(captured.count(), 1);
  QCOMPARE(advanced.count(), 2);
  QCOMPARE(advanced.at(1).at(1).toString(), QStringLiteral("On target"));
  QTest::keyClick(&controls, Qt::Key_Escape);
  QCOMPARE(dismissed.count(), 1);
  QCOMPARE(committed.count(), 1);
  QCOMPARE(committed.at(0).at(0).toString(), QStringLiteral("Shot"));
  QCOMPARE(committed.at(0).at(1).toString(), QString());

  captured.clear();
  QSignalSpy gameStarted(&controls, &GameControls::gameStartRequested);
  QSignalSpy nextQuarter(&controls, &GameControls::nextQuarterRequested);
  QTest::keyClick(&controls, Qt::Key_G);
  QCOMPARE(captured.count(), 0);
  QCOMPARE(gameStarted.count(), 1);
  QTest::keyClick(&controls, Qt::Key_H);
  QCOMPARE(captured.count(), 0);
  QCOMPARE(nextQuarter.count(), 1);
}

void EventCatalogTest::goalPenaltyAndShootoutUseCatalogFlags() {
  const EventCatalog::Node* scoringGoal = EventCatalog::scoringGoalMainEvent();
  QVERIFY(scoringGoal != nullptr);
  QCOMPARE(scoringGoal->id, EventCatalog::EventId::Goal);
  QVERIFY(scoringGoal->mainEvent);
  QVERIFY(scoringGoal->scoresGoal);
  QVERIFY(!scoringGoal->continuesAsGoal);
  QVERIFY(!scoringGoal->isShootout);
  QCOMPARE(EventCatalog::shootoutAttemptResult(scoringGoal->id),
           EventCatalog::ShootoutAttemptResult::None);
  QCOMPARE(QLatin1String(scoringGoal->xmlCode), QLatin1String("GOAL"));
  QVERIFY(EventCatalog::mainEventScoresGoal(scoringGoal->id));
  QVERIFY(EventCatalog::scoresGoal(scoringGoal->id));

  const EventCatalog::Node* shootout = nullptr;
  const EventCatalog::Node* scoringFollowUp = nullptr;
  EventCatalog::EventId convertedAttempt = EventCatalog::EventId::None;
  int scoringMainEvents = 0;
  int scoringFollowUps = 0;
  int continuingFollowUps = 0;
  int shootoutMainEvents = 0;
  QSet<int> attemptResults;
  for (int index = 0; index < EventCatalog::nodeCount(); ++index) {
    const EventCatalog::Node& node = EventCatalog::nodeAt(index);
    QCOMPARE(EventCatalog::scoresGoal(node.id), node.scoresGoal);
    QCOMPARE(EventCatalog::continuesAsGoal(node.id), node.continuesAsGoal);
    QCOMPARE(EventCatalog::isShootout(node.id), node.isShootout);
    QCOMPARE(EventCatalog::shootoutAttemptResult(node.id), node.shootoutAttemptResult);

    if (node.mainEvent && node.scoresGoal) ++scoringMainEvents;
    if (!node.mainEvent && node.scoresGoal) {
      ++scoringFollowUps;
      scoringFollowUp = &node;
      QCOMPARE(node.id, EventCatalog::EventId::FollowUpGoal);
      QVERIFY(node.continuesAsGoal);
    }
    if (node.continuesAsGoal) {
      ++continuingFollowUps;
      QVERIFY(!node.mainEvent);
      QVERIFY(node.scoresGoal);
    }
    if (node.isShootout) {
      ++shootoutMainEvents;
      shootout = &node;
      QCOMPARE(node.id, EventCatalog::EventId::Shootout);
      QVERIFY(node.mainEvent);
      QVERIFY(!node.scoresGoal);
    }
    if (node.shootoutAttemptResult != EventCatalog::ShootoutAttemptResult::None) {
      QVERIFY(!node.mainEvent);
      QVERIFY(!node.scoresGoal);
      QVERIFY(!node.continuesAsGoal);
      QVERIFY(!node.isShootout);
      const int result = static_cast<int>(node.shootoutAttemptResult);
      QVERIFY(!attemptResults.contains(result));
      attemptResults.insert(result);
      if (node.shootoutAttemptResult == EventCatalog::ShootoutAttemptResult::Converted) {
        convertedAttempt = node.id;
      }
    }
  }
  QCOMPARE(scoringMainEvents, 1);
  QCOMPARE(scoringFollowUps, 1);
  QCOMPARE(continuingFollowUps, 1);
  QCOMPARE(shootoutMainEvents, 1);
  QCOMPARE(attemptResults.size(), 3);
  QVERIFY(scoringFollowUp != nullptr);
  QVERIFY(shootout != nullptr);
  QCOMPARE(convertedAttempt, EventCatalog::EventId::Converted);
  QVERIFY(TagEvent::containsScoredGoal(TagEvent::chainOf(scoringFollowUp->id)));
  QVERIFY(!TagEvent::containsScoredGoal(TagEvent::chainOf(EventCatalog::EventId::Converted)));
  QVERIFY(!TagEvent::containsScoredGoal(TagEvent::chainOf(EventCatalog::EventId::NoGoal)));
  QVERIFY(!TagEvent::containsScoredGoal({}));

  const EventCatalog::EventId plainMains[] = {
      EventCatalog::EventId::PenaltyCorner, EventCatalog::EventId::PenaltyStroke,
      EventCatalog::EventId::Shot,          EventCatalog::EventId::Pass,
      EventCatalog::EventId::CircleEntry,
  };
  for (const EventCatalog::EventId id : plainMains) {
    const EventCatalog::Node* node = EventCatalog::nodeForId(id);
    QVERIFY(node != nullptr);
    QVERIFY(node->mainEvent);
    QVERIFY(!node->scoresGoal);
    QVERIFY(!node->continuesAsGoal);
    QVERIFY(!node->isShootout);
    QVERIFY(!EventCatalog::mainEventScoresGoal(id));
  }

  for (int index = 0; index < EventCatalog::nodeCount(); ++index) {
    const EventCatalog::Node& node = EventCatalog::nodeAt(index);
    if (!node.mainEvent) continue;
    QStringList selections;
    QCOMPARE(FollowUpCatalog::continuesAsGoal(QString::fromLatin1(node.canonicalName), selections),
             false);
    expectLeafContinuationMatchesFlag(node, node, &selections);
  }

  const EventCatalog::EventId chainingMains[] = {
      EventCatalog::EventId::PenaltyCorner,
      EventCatalog::EventId::PenaltyStroke,
      EventCatalog::EventId::Shot,
  };
  QVector<EventCatalog::EventId> shotPath;
  for (const EventCatalog::EventId id : chainingMains) {
    const EventCatalog::Node* node = EventCatalog::nodeForId(id);
    QVERIFY(node != nullptr);
    QVector<EventCatalog::EventId> path;
    QVERIFY(findContinuation(*node, &path));
    QVERIFY(!path.isEmpty());
    QCOMPARE(path.constLast(), scoringFollowUp->id);
    QVERIFY(TagEvent::containsScoredGoal(chainFromIds(path)));
    if (id == EventCatalog::EventId::Shot) shotPath = path;
  }
  QCOMPARE(shotPath.size(), 2);
  QVector<EventCatalog::EventId> shootoutPath;
  QVERIFY(!findContinuation(*shootout, &shootoutPath));
  QVERIFY(shootoutPath.isEmpty());

  QVERIFY(FollowUpCatalog::continuesAsGoal(QStringLiteral("PS"), {QStringLiteral("Goal")}));
  QVERIFY(FollowUpCatalog::continuesAsGoal(
      QStringLiteral("PC"),
      {QStringLiteral("Direct shot"), QStringLiteral("Hit"), QStringLiteral("Goal")}));
  QVERIFY(FollowUpCatalog::continuesAsGoal(QStringLiteral("PC"),
                                           {QStringLiteral("Variant"), QStringLiteral("Goal")}));
  QVERIFY(FollowUpCatalog::continuesAsGoal(QStringLiteral("PC"),
                                           {QStringLiteral("Ruined"), QStringLiteral("Goal")}));
  QVERIFY(!FollowUpCatalog::continuesAsGoal(QStringLiteral("PC"),
                                            {QStringLiteral("Ruined"), QStringLiteral("No Goal")}));
  QVERIFY(!FollowUpCatalog::continuesAsGoal(
      QStringLiteral("PC"), {QStringLiteral("Ruined"), QStringLiteral("New PC")}));
  QVERIFY(FollowUpCatalog::continuesAsGoal(
      QStringLiteral("Shot"), {QStringLiteral("On target"), QStringLiteral("Goal")}));
  QVERIFY(!FollowUpCatalog::continuesAsGoal(
      QStringLiteral("Shot"), {QStringLiteral("On target"), QStringLiteral("Saved")}));
  QVERIFY(!FollowUpCatalog::continuesAsGoal(QStringLiteral("Shot"), {QStringLiteral("On target")}));
  QVERIFY(!FollowUpCatalog::continuesAsGoal(QStringLiteral("S.O."), {QStringLiteral("Converted")}));
  QVERIFY(!FollowUpCatalog::continuesAsGoal(QStringLiteral("PS"), {QStringLiteral("No Goal")}));
  QVERIFY(!FollowUpCatalog::continuesAsGoal(QStringLiteral("PS"), {}));

  TagSession exportSession;
  configureMatch(&exportSession);
  exportSession.addTag(playedTag(EventCatalog::EventId::Shot, QStringLiteral("Home"), 30000,
                                 chainFromIds(shotPath)));
  exportSession.addTag(playedTag(shootout->id, QStringLiteral("Home"), 90000,
                                 TagEvent::chainOf(convertedAttempt)));
  const QVector<XmlExporter::ExportedXmlInstance> exported =
      XmlExporter::buildExportedInstances(&exportSession);
  int goalPlusCount = 0;
  int shootoutPlusCount = 0;
  const QString goalPlus =
      QStringLiteral("UC %1+").arg(QLatin1String(scoringGoal->xmlCode));
  const QString shootoutPlus = QStringLiteral("UC %1+").arg(QLatin1String(shootout->xmlCode));
  const QString shotPlus = QStringLiteral("UC SHOT+");
  bool shootoutScoreIsOneNil = false;
  for (const XmlExporter::ExportedXmlInstance& instance : exported) {
    if (instance.code == goalPlus) ++goalPlusCount;
    if (instance.code == shootoutPlus) {
      ++shootoutPlusCount;
      for (const XmlExporter::ExportedXmlLabel& label : instance.labels) {
        if (label.group == QStringLiteral("RESULTADO") &&
            label.text == QStringLiteral("UC 1 - 0 OR")) {
          shootoutScoreIsOneNil = true;
        }
      }
    }
    QVERIFY(instance.code != QStringLiteral("UC GOAL"));
  }
  int shotPlusCount = 0;
  for (const XmlExporter::ExportedXmlInstance& instance : exported) {
    if (instance.code == shotPlus) ++shotPlusCount;
  }
  QCOMPARE(shotPlusCount, 1);
  QCOMPARE(goalPlusCount, 1);
  QCOMPARE(shootoutPlusCount, 1);
  QVERIFY(shootoutScoreIsOneNil);
  QVERIFY(!exported.isEmpty());

  TagSession confirmedGoal;
  configureMatch(&confirmedGoal);
  confirmedGoal.addTag(playedTag(EventCatalog::EventId::Shot, QStringLiteral("Home"), 30000,
                                 chainFromIds(shotPath)));
  confirmedGoal.addTag(playedTag(scoringGoal->id, QStringLiteral("Home"), 31000));
  const QVector<XmlExporter::ExportedXmlInstance> withExplicitGoal =
      XmlExporter::buildExportedInstances(&confirmedGoal);
  int confirmedGoalCount = 0;
  for (const XmlExporter::ExportedXmlInstance& instance : withExplicitGoal) {
    if (instance.code == goalPlus) ++confirmedGoalCount;
  }
  QCOMPARE(confirmedGoalCount, 1);

  const ShootoutState shootoutState = ShootoutState::fromGameTags(exportSession.tags());
  QVERIFY(shootoutState.hasAttempts());
  const ShootoutState::Snapshot revealed =
      shootoutState.snapshotRevealedThrough(90000, AppLocale::Language::English);
  QCOMPARE(revealed.homeConverted, 1);
  QCOMPARE(revealed.awayConverted, 0);

  TagSession scoreboardSession;
  configureMatch(&scoreboardSession);
  scoreboardSession.addTag(playedTag(scoringGoal->id, QStringLiteral("Home"), 8000));
  const TagSession::GameTag storedGoal = scoreboardSession.tags().constLast();
  scoreboardSession.addTag(
      playedTag(shootout->id, QStringLiteral("Home"), 50000, TagEvent::chainOf(convertedAttempt)));
  const TagSession::GameTag storedShootout = scoreboardSession.tags().constLast();
  scoreboardSession.addTag(playedTag(EventCatalog::EventId::Pass, QStringLiteral("Away"), 80000));
  const TagSession::GameTag storedLater = scoreboardSession.tags().constLast();

  auto presentationClip = [](const TagSession::GameTag& tag) {
    PresentationQueue::Clip clip;
    clip.tagId = tag.id;
    clip.markMs = tag.markMs;
    clip.startMs = tag.startMs;
    clip.endMs = tag.endMs;
    clip.mainEvent = tag.mainEventKey();
    clip.followUps = tag.followUpChain();
    clip.team = tag.team;
    return clip;
  };
  ExportClipBuilder::OverlayOptions options;
  options.includeBottomOverlay = false;
  options.includeNotesOverlay = false;
  options.includeScoreboardOverlay = true;
  const QVector<ClipSegment> segments = ExportClipBuilder::buildClipSegments(
      &scoreboardSession,
      {presentationClip(storedGoal), presentationClip(storedShootout),
       presentationClip(storedLater)},
      options);
  QCOMPARE(segments.size(), 3);
  QCOMPARE(segments.at(0).scoreboards.at(1).scoreboard.homeGoals, 1);
  QVERIFY(segments.at(0).shootouts.isEmpty());
  QCOMPARE(segments.at(1).scoreboards.at(0).scoreboard.homeGoals, 1);
  QCOMPARE(segments.at(1).scoreboards.at(0).scoreboard.awayGoals, 0);
  QCOMPARE(segments.at(1).shootouts.size(), 2);
  QCOMPARE(segments.at(1).shootouts.at(1).frame.homeConverted, 1);
  QCOMPARE(segments.at(2).scoreboards.at(0).scoreboard.homeGoals, 1);
  QCOMPARE(segments.at(2).scoreboards.at(0).scoreboard.awayGoals, 0);
  QCOMPARE(segments.at(2).shootouts.at(0).frame.homeConverted, 1);

  GameControls controls;
  controls.setTeamSide(TagSession::TeamSide::Home);
  controls.resize(420, 780);
  controls.show();
  QSignalSpy captured(&controls, &GameControls::mainEventTimestampCaptured);
  QSignalSpy committed(&controls, &GameControls::tagCommitted);
  const QString arrow = AppLocale::kCompoundPathSeparator;

  const EventCatalog::Node* penaltyStroke =
      EventCatalog::nodeForId(EventCatalog::EventId::PenaltyStroke);
  QVERIFY(penaltyStroke != nullptr);
  int penaltyStrokeGoalIndex = -1;
  for (int childIndex = 0; childIndex < penaltyStroke->childCount; ++childIndex) {
    if (EventCatalog::continuesAsGoal(
            penaltyStroke->children.at(static_cast<std::size_t>(childIndex)))) {
      penaltyStrokeGoalIndex = childIndex;
    }
  }
  QCOMPARE(penaltyStrokeGoalIndex, 0);
  QTest::keyClick(&controls, penaltyStroke->shortcut);
  QCOMPARE(captured.count(), 1);
  QCOMPARE(captured.at(0).at(0).toString(), QStringLiteral("PS"));
  QCOMPARE(committed.count(), 0);
  QTest::keyClick(&controls, static_cast<Qt::Key>(Qt::Key_1 + penaltyStrokeGoalIndex));
  QCOMPARE(committed.count(), 2);
  QCOMPARE(committed.at(0).at(0).toString(), QStringLiteral("PS"));
  QCOMPARE(committed.at(0).at(1).toString(), QStringLiteral("home") + arrow + QStringLiteral("Goal"));
  QCOMPARE(committed.at(1).at(0).toString(), QString::fromLatin1(scoringGoal->canonicalName));
  QCOMPARE(committed.at(1).at(1).toString(), QStringLiteral("home"));
  QCOMPARE(captured.count(), 2);
  QCOMPARE(captured.at(1).at(0).toString(), QString::fromLatin1(scoringGoal->canonicalName));

  captured.clear();
  committed.clear();
  const EventCatalog::Node* penaltyCorner =
      EventCatalog::nodeForId(EventCatalog::EventId::PenaltyCorner);
  QVERIFY(penaltyCorner != nullptr);
  QTest::keyClick(&controls, penaltyCorner->shortcut);
  QTest::keyClick(&controls, Qt::Key_1);
  QTest::keyClick(&controls, Qt::Key_1);
  QCOMPARE(committed.count(), 0);
  QTest::keyClick(&controls, Qt::Key_1);
  QCOMPARE(committed.count(), 2);
  QCOMPARE(committed.at(0).at(0).toString(), QStringLiteral("PC"));
  QCOMPARE(committed.at(0).at(1).toString(),
           QStringLiteral("home") + arrow + QStringLiteral("Direct shot") + arrow +
               QStringLiteral("Hit") + arrow + QStringLiteral("Goal"));
  QCOMPARE(committed.at(1).at(0).toString(), QString::fromLatin1(scoringGoal->canonicalName));
  QCOMPARE(committed.at(1).at(1).toString(), QStringLiteral("home"));

  captured.clear();
  committed.clear();
  QTest::keyClick(&controls, shootout->shortcut);
  QTest::keyClick(&controls, Qt::Key_1);
  QCOMPARE(captured.count(), 1);
  QCOMPARE(captured.at(0).at(0).toString(), QStringLiteral("S.O."));
  QCOMPARE(committed.count(), 1);
  QCOMPARE(committed.at(0).at(0).toString(), QStringLiteral("S.O."));
  QCOMPARE(committed.at(0).at(1).toString(),
           QStringLiteral("home") + arrow + QStringLiteral("Converted"));
}

int runEventCatalogTests(int argc, char** argv) {
  EventCatalogTest tests;
  return QTest::qExec(&tests, argc, argv);
}

#include "EventCatalogTests.moc"
