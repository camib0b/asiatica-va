#pragma once

#include <QString>
#include <QStringList>

#include <Qt>
#include <array>

namespace EventCatalog {

/// Longest follow-up chain under a main event (main → 1 → 2 → 3).
inline constexpr int kMaxFollowUpDepth = 3;

/// Widest follow-up row. Pass, Turnover, Special, and PC Foul each have four.
inline constexpr int kMaxFollowUpChildren = 4;

/// Stable identity for a main event or a follow-up.
///
/// Values are part of the catalog contract. Do not renumber or reuse them.
/// Game tags, .clips.json, and QSettings still store the canonical English
/// name; these ids are not written yet.
enum class EventId : int {
  None = 0,

  // Main events, in the clip-duration settings order.
  Goal = 1,
  Shot = 2,
  PenaltyCorner = 3,
  PenaltyCornerFoul = 4,
  Card = 5,
  Pass = 6,
  CircleEntry = 7,
  SixteenYard = 8,
  FiftyYard = 9,
  SeventyFiveYard = 10,
  Turnover = 11,
  Special = 12,
  PenaltyStroke = 13,
  Shootout = 14,

  // Follow-ups. A repeated English word is one id when the children match
  // (Left, outcome Goal, No Goal). It is a different id when the children
  // differ (main Goal vs outcome Goal, main Pass vs circle-entry Pass,
  // pass Hit vs penalty-corner Hit).
  OnTarget = 100,
  OffTarget = 101,
  Blocked = 102,
  Saved = 103,
  Post = 104,
  Closeby = 105,
  NotClose = 106,
  FollowUpGoal = 107,
  NoGoal = 108,
  Dribbling = 109,
  CircleEntryPass = 110,
  Deflection = 111,
  Left = 112,
  Middle = 113,
  Right = 114,
  DirectShot = 115,
  Variant = 116,
  Ruined = 117,
  DirectShotHit = 118,
  Swept = 119,
  Dragflick = 120,
  NewPc = 121,
  ThreeMan = 122,
  FourMan = 123,
  Fast = 124,
  Forward = 125,
  Sideways = 126,
  Back = 127,
  Green = 128,
  Yellow = 129,
  Red = 130,
  Flick = 131,
  Push = 132,
  Sweep = 133,
  PassHit = 134,
  Completed = 135,
  Failed = 136,
  Good = 137,
  Bad = 138,
  Neutral = 139,
  Referee = 140,
  Interception = 141,
  Tackle = 142,
  Pressure = 143,
  UnforcedError = 144,
  Foot = 145,
  Stick = 146,
  Danger = 147,
  Other = 148,
  Converted = 149,
  Missed = 150,
  Replay = 151,
};

/// How a committed tag joins the team label with the follow-up chain.
enum class PayloadLayout {
  PlainChain,     ///< selections joined with " → "
  TeamThenChain,  ///< team → selections
  ChainThenTeam,  ///< selections → team
  TeamOnly,       ///< team label; ignores selections
};

/// One catalog row. Main events and follow-ups share this shape.
/// Follow-ups leave shortcut, XML code, lead/lag, and payload unused.
struct Node {
  EventId id = EventId::None;
  /// English token stored on tags, buttons, and clip-duration QSettings.
  const char* canonicalName = nullptr;
  Qt::Key shortcut = Qt::Key_unknown;
  /// Short XML code for a main event. Null for follow-ups.
  const char* xmlCode = nullptr;
  qint64 leadMs = 0;
  qint64 lagMs = 0;
  /// Spanish UI text. Null means Spanish shows the canonical name.
  const char* spanish = nullptr;
  /// Shown in every language instead of the canonical name. Null means no override.
  const char* displayLabel = nullptr;
  PayloadLayout payloadLayout = PayloadLayout::PlainChain;
  bool switchesTeam = false;
  bool mainEvent = false;
  std::array<EventId, kMaxFollowUpChildren> children{};
  int childCount = 0;
};

int nodeCount();
const Node& nodeAt(int index);
const Node* nodeForId(EventId id);

/// Exact canonical-name match among main events. "Pass" is the main event,
/// not the circle-entry follow-up that shares the word.
const Node* mainEventForCanonicalName(const QString& canonicalName);

/// True when any row (main event or follow-up) uses this exact canonical name.
bool containsCanonicalName(const QString& canonicalName);

QString canonicalName(EventId id);
Qt::Key shortcutKey(EventId id);
/// "Q".."Z" for a letter shortcut. Empty when the event has no letter key.
QString shortcutLabel(EventId id);

/// Spanish text for an exact canonical name, or empty when the name is absent
/// or Spanish keeps the English token. Rows that share a name agree.
QString spanishForCanonicalName(const QString& canonicalName);

/// Display override for a token, compared without case after trimming.
/// Empty when the token has no override. "special" resolves to the star.
QString displayLabelForToken(const QString& token);

PayloadLayout payloadLayoutForMainEvent(const QString& canonicalName);
bool switchesTeamForMainEvent(const QString& canonicalName);

QStringList childCanonicalNames(const Node& node);
const Node* childForCanonicalName(const Node& parent, const QString& canonicalName);

}  // namespace EventCatalog
