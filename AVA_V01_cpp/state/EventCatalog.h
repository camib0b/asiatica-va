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

/// Buttons across one GameControls keyboard row. A shorter row stays left-aligned.
inline constexpr int kButtonGridColumnCount = 4;

/// Layout weight of one key. Button-row offsets use the same units, so 1 is a
/// quarter key and 3 is three quarters of a key.
inline constexpr int kButtonGridUnitsPerKey = 4;

/// buttonRow and buttonColumn when the node is not on the GameControls grid.
inline constexpr int kNoButtonGridPosition = -1;

/// Stable identity for a main event or a follow-up.
///
/// Values are part of the catalog contract. Do not renumber or reuse them.
/// Game tags store these ids. XML export, .clips.json, and clip-duration
/// QSettings still use the canonical English name.
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

/// Result stored on a shoot-out follow-up. None means this row is not one.
/// Converted scores the shoot-out board only. It does not score the match.
enum class ShootoutAttemptResult {
  None,
  Converted,
  Missed,
  Replay,
};

/// One catalog row. Main events and follow-ups share this shape.
/// Follow-ups leave shortcut, XML code, lead/lag, payload, and the button grid unused.
struct Node {
  EventId id = EventId::None;
  /// English token. XML export, .clips.json, and clip-duration QSettings still
  /// write this name. Game tags store EventId instead.
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
  /// Main event: this tag counts on the match scoreboard and in XML RESULTADO.
  /// Follow-up: the chain records a scored goal, so export can write a synthetic
  /// GOAL code. A shoot-out conversion does not set this flag.
  bool scoresGoal = false;
  /// Committing a chain that ends on this follow-up starts the scoring-goal event.
  bool continuesAsGoal = false;
  /// This main event is a shoot-out attempt. Its results stay off the match score.
  bool isShootout = false;
  ShootoutAttemptResult shootoutAttemptResult = ShootoutAttemptResult::None;
  /// GameControls keyboard row, top to bottom. kNoButtonGridPosition for follow-ups.
  int buttonRow = kNoButtonGridPosition;
  /// Left-to-right slot within buttonRow. kNoButtonGridPosition for follow-ups.
  int buttonColumn = kNoButtonGridPosition;
  /// Left spacer for this button's row, in kButtonGridUnitsPerKey units.
  /// Zero for follow-ups. Every button in a row uses the same offset.
  int buttonRowOffsetUnits = 0;
  std::array<EventId, kMaxFollowUpChildren> children{};
  int childCount = 0;
};

/// One horizontal row of the GameControls keyboard, top to bottom.
/// Events are buttonGridNodeAt(firstEventIndex + column).
struct ButtonGridRow {
  int leftOffsetUnits = 0;
  int firstEventIndex = 0;
  int eventCount = 0;
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

/// UI label for one id. A display override wins in every language. Spanish
/// uses the Spanish text when that text is set; otherwise the canonical name.
/// Empty when \p id is not in the catalog. Main Goal and outcome Goal stay
/// distinct ids and share the same label.
QString labelForId(EventId id, bool spanish);

PayloadLayout payloadLayoutForMainEvent(const QString& canonicalName);
bool switchesTeamForMainEvent(const QString& canonicalName);

/// Raw catalog flags. False / None when \p id is not in the catalog.
bool scoresGoal(EventId id);
bool continuesAsGoal(EventId id);
bool isShootout(EventId id);
ShootoutAttemptResult shootoutAttemptResult(EventId id);

/// True when \p id is a main event whose scoresGoal flag is set.
bool mainEventScoresGoal(EventId id);

/// The one main event with scoresGoal set. Null when the catalog has none.
const Node* scoringGoalMainEvent();

QStringList childCanonicalNames(const Node& node);
const Node* childForCanonicalName(const Node& parent, const QString& canonicalName);

/// Main events in GameControls keyboard order: increasing button row, then column.
int buttonGridCount();
const Node& buttonGridNodeAt(int index);
int buttonGridRowCount();
ButtonGridRow buttonGridRowAt(int rowIndex);

}  // namespace EventCatalog
