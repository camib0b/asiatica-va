#pragma once

#include "EventLabelOverrides.h"
#include "TagEvent.h"

#include <QDate>
#include <QObject>
#include <QHash>
#include <QString>
#include <QVector>
#include <QtGlobal>

#include <array>

class TagSession final : public QObject {
  Q_OBJECT

public:
  struct GameTag {
    quint64 id = 0;         // Stable mention identity; assigned on add/import.
    EventCatalog::EventId mainEventId = EventCatalog::EventId::None;
    /// Time-control code or any other token that is not a catalog main event.
    /// Empty when mainEventId is set.
    QString unlistedName;
    std::array<EventCatalog::EventId, EventCatalog::kMaxFollowUpDepth> followUpIds{};
    int followUpCount = 0;
    qint64 markMs = 0;      // Event mark (click moment): the anchor used for table seek/highlight.
    qint64 startMs = 0;     // Clip interval start (used by exporters).
    qint64 endMs = 0;       // Clip interval end (used by exporters).
    QString note;
    QString period;   // e.g. "Q1", "Q2", "Q3", "Q4"
    QString team;  ///< TeamSide key: "Home" or "Away" (see teamSideFromKey).
    bool intervalManuallyEdited = false; // true when the user trimmed start/end away from defaults

    TagEvent::MainEventKey mainEventKey() const {
      if (mainEventId != EventCatalog::EventId::None) return TagEvent::catalogKey(mainEventId);
      return TagEvent::unlistedKey(unlistedName);
    }

    void setMainEventKey(const TagEvent::MainEventKey& key) {
      if (key.id != EventCatalog::EventId::None) {
        mainEventId = key.id;
        unlistedName.clear();
        return;
      }
      mainEventId = EventCatalog::EventId::None;
      unlistedName = key.unlistedName;
    }

    TagEvent::FollowUpChain followUpChain() const {
      TagEvent::FollowUpChain chain;
      const int used = followUpCount < 0
                           ? 0
                           : (followUpCount > EventCatalog::kMaxFollowUpDepth
                                  ? EventCatalog::kMaxFollowUpDepth
                                  : followUpCount);
      chain.count = used;
      for (int index = 0; index < used; ++index) {
        chain.ids[static_cast<std::size_t>(index)] = followUpIds[static_cast<std::size_t>(index)];
      }
      return chain;
    }

    void setFollowUpChain(const TagEvent::FollowUpChain& chain) {
      const int used = chain.size();
      followUpCount = used;
      followUpIds = {};
      for (int index = 0; index < used; ++index) {
        followUpIds[static_cast<std::size_t>(index)] = chain.at(index);
      }
    }
  };

  enum class QuarterPhase {
    /// No game has started yet (Start-Game button not pressed).
    NotStarted,
    /// A quarter is in progress (currentQuarterIndex_ holds 0..3).
    QuarterInProgress,
    /// All four quarters have finished (Q4 closed).
    GameEnded,
  };

  enum class ImportMode {
    Replace,
    Merge,
  };

  /// Stored in GameTag::team. Unspecified covers empty, Neutral, and any other value.
  enum class TeamSide {
    Unspecified,
    Home,
    Away,
  };

  /// Interprets GameTag::team. Trims whitespace and matches "Home"/"Away" case-insensitively.
  static TeamSide teamSideFromKey(const QString& teamKey);
  /// Canonical GameTag::team key for \p side. Empty for Unspecified.
  static QString teamSideKey(TeamSide side);

  /// Outcome of importTags(). Skipping happens in XmlEventMappingDialog before
  /// tags reach this API; importTags stores every remaining tag (clamping intervals).
  struct ImportResult {
    int importedCount = 0;
    int clampedCount = 0;
  };

  explicit TagSession(QObject* parent = nullptr);
  ~TagSession() override = default;

  /// Drops tags, notes, and game-time state. Label overrides stay: they belong
  /// to the session, not to the tag list.
  void clear();
  /// Bumps when clear() or a replace import restarts tag ids at 1.
  /// Pending clip notes capture this so a reused id cannot accept a stale edit.
  quint64 tagIdEpoch() const { return tagIdEpoch_; }
  /// Clears team names/colors plus date and abbreviations. Does not clear tags.
  /// Emits gameMetadataChanged() when any field actually changes.
  void clearGameMetadata();
  void setGameTeams(const QString& homeName, const QString& awayName,
                   const QString& homeColor, const QString& awayColor);
  /// Updates date / abbreviations metadata used by the XML exporter.
  /// Empty strings clear the corresponding field.
  void setGameMetadata(const QDate& gameDate,
                       const QString& homeAbbrev,
                       const QString& awayAbbrev);

  QString homeTeamName() const { return homeTeamName_; }
  QString awayTeamName() const { return awayTeamName_; }
  QString homeTeamColor() const { return homeTeamColor_; }
  QString awayTeamColor() const { return awayTeamColor_; }
  QDate gameDate() const { return gameDate_; }
  int gameYear() const { return gameDate_.isValid() ? gameDate_.year() : 0; }
  QString homeAbbrev() const { return homeAbbrev_; }
  QString awayAbbrev() const { return awayAbbrev_; }

  /// Renames, shortcut keys, and hidden flags for this session.
  /// See EventLabelOverrides. An id-level label or hidden flag applies to every
  /// instance and clears path exceptions for that field. Display text reads the
  /// label. GameControls also reads the shortcut and the hidden flag.
  const EventLabelOverrides::Table& eventLabelOverrides() const { return eventLabelOverrides_; }
  EventLabelOverrides::Result setEventLabel(EventCatalog::EventId id, const QString& label);
  EventLabelOverrides::Result clearEventLabel(EventCatalog::EventId id);
  EventLabelOverrides::Result setEventShortcutKey(EventCatalog::EventId id, Qt::Key key);
  EventLabelOverrides::Result clearEventShortcutKey(EventCatalog::EventId id);
  EventLabelOverrides::Result setEventHidden(EventCatalog::EventId id, bool hidden);
  /// Drops every override. TagSession::clear() does not call this.
  void clearEventLabelOverrides();
  /// Replaces the override table. Returns false when \p overrides is not a
  /// valid setup; the previous table is kept.
  bool replaceEventLabelOverrides(const EventLabelOverrides::Table& overrides);

  /// Next id that addTag will assign. Restored sessions keep this above every
  /// saved id so a deleted tag's id is not reused.
  quint64 nextTagId() const { return nextTagId_; }

  void addTag(const GameTag& tag);
  ImportResult importTags(const QVector<GameTag>& tags,
                          ImportMode mode,
                          qint64 videoDurationMs = -1);
  /// Replaces the tag list in \p tags order. Unlike importTags, this does not
  /// sort. Ids already set on the tags are kept. \p nextTagId is a floor.
  void restoreTags(const QVector<GameTag>& tags, quint64 nextTagId);
  /// Returns false when \p index is out of range (Q_ASSERT in debug builds).
  bool removeTag(int index);
  /// Returns false when \p index is out of range (Q_ASSERT in debug builds).
  bool setTagNote(int index, const QString& note);
  /// Returns an empty string when \p index is out of range. Call isValidTagIndex first
  /// when an empty note and a miss must be distinguished.
  QString tagNote(int index) const;

  /// Freeform match notes (HTML); not tied to a single clip. Mentions use ava-tag:id anchors.
  void setMatchNote(const QString& html);
  QString matchNote() const { return matchNote_; }

  /// Index of the tag with \p id, or -1 if it is not in the session.
  int indexOfTagId(quint64 id) const;
  /// True when \p index selects an element of tags().
  bool isValidTagIndex(int index) const { return index >= 0 && index < tags_.size(); }
  /// Updates the clip interval (start/end in ms) of the tag at \p index and marks it as
  /// manually trimmed so later default-duration changes cannot overwrite it.
  /// Returns false when \p index is out of range (Q_ASSERT in debug builds).
  bool setTagInterval(int index, qint64 startMs, qint64 endMs);
  /// Re-applies lead/lag defaults (in ms) to every tag of \p mainEvent that has not been
  /// manually trimmed. Quarter / start-anchor tags are skipped because their interval is
  /// determined by user clicks, not by symmetric pads.
  void applyDefaultsToUntrimmedTags(const QString& mainEvent, qint64 leadMs, qint64 lagMs);

  // ---- Game-time anchor & quarter tracking ----
  // Quarter phase, the in-progress index, and the start anchor are derived from
  // quarter tags. startGame() and advanceQuarter() are the only mutators; they
  // add the tag, restore state, and emit through addTag().
  qint64 gameStartAnchorMs() const { return gameStartAnchorMs_; }
  bool hasGameStartAnchor() const { return gameStartAnchorMs_ >= 0; }

  QuarterPhase quarterPhase() const { return quarterPhase_; }
  /// Index of the quarter currently in progress (0=Q1 .. 3=Q4).
  /// -1 when not in progress (NotStarted or GameEnded). Do not treat GameEnded as Q4
  /// in progress — period labels after the whistle use closed Q4 in
  /// periodLabelAtTimestampMs() and currentPeriodLabel(), not this index.
  int currentQuarterIndex() const { return currentQuarterIndex_; }
  /// Playhead position when the in-progress quarter started.
  qint64 currentQuarterStartMs() const { return currentQuarterStartMs_; }

  /// Inserts the start-anchor tag and lets restore open Q1. Returns false when
  /// the game has already started. Emits tagAdded and tagsChanged on success.
  bool startGame(qint64 anchorMs);
  /// Closes the in-progress quarter at \p nowMs and lets restore open the next
  /// quarter or end the game. Returns false when no quarter is in progress.
  /// Emits tagAdded and tagsChanged on success.
  bool advanceQuarter(qint64 nowMs);

  /// Quarter label from restored game-time state (closed Q1–Q4 spans plus the
  /// in-progress or ended quarter). Empty if the timestamp is before the game.
  QString periodLabelAtTimestampMs(qint64 positionMs) const;
  /// Period stored on a newly tagged event: the in-progress quarter, or Q4 once
  /// the game has ended. Empty before the game starts. Not a playhead lookup.
  QString currentPeriodLabel() const;

  const QVector<GameTag>& tags() const { return tags_; }
  const QHash<TagEvent::MainEventKey, int>& mainEventCounts() const { return mainEventCounts_; }
  const QHash<TagEvent::MainEventKey, QHash<TagEvent::FollowUpChain, int>>& followUpCountsByMainEvent()
      const {
    return followUpCountsByMainEvent_;
  }

signals:
  /// Emitted before clear() drops tags or restarts ids. Flush pending notes here.
  void aboutToClear();
  /// Emitted before importTags() mutates the tag list, including merge.
  /// A replace import restarts ids after this signal. Flush pending notes here.
  void aboutToReplaceTags();
  void cleared();
  void tagAdded(const TagSession::GameTag& tag);
  void tagsImported();
  void tagNoteChanged(int index);
  void matchNoteChanged();
  void tagIntervalChanged(int index);
  /// Tag membership, order, or event counts changed. Not emitted for game metadata.
  /// \p addedTagId is the id just inserted by addTag(); 0 for every other change.
  /// Listeners that flash the new row must use this argument. tagAdded is emitted
  /// first, but the row model is rebuilt on this signal, so tagAdded is too early.
  void tagsChanged(quint64 addedTagId = 0);
  /// Team names/colors, date, or abbreviations changed.
  void gameMetadataChanged();
  /// A label, shortcut, or hidden override was stored or cleared.
  void eventLabelOverridesChanged();

private:
  static constexpr int kQuarterCount = 4;

  struct ClosedQuarterSpan {
    bool present = false;
    qint64 startMs = 0;
    qint64 endMs = 0;
  };

  /// Start-anchor and Q1–Q4 tags only, in tags_ order. Timeouts are omitted.
  struct GameTimeTagRecord {
    quint64 id = 0;
    int quarterIndex = -1;  // -1 = start-anchor; 0..3 = Q1..Q4
    qint64 startMs = 0;
    qint64 endMs = 0;
  };

  /// Rebuilds mainEventCounts_ and followUpCountsByMainEvent_ from tags_. Call after
  /// any mutation that adds or removes tags — not only after import/clear.
  void rebuildEventCountsFromTags();
  /// Rebuilds gameTimeTags_ (start-anchor + Q1–Q4 only) from tags_. Call after import
  /// or any bulk rewrite of tags_. Incremental add/remove/interval updates the index
  /// in place so restore does not scan play tags.
  void rebuildGameTimeIndexFromTags();
  void upsertGameTimeTag(const GameTag& tag);
  void removeGameTimeTagId(quint64 id);
  /// Rebuilds phase, current quarter, start anchor, and closed-quarter spans from
  /// gameTimeTags_. Call after the index changes — not after ordinary play-tag edits.
  void restoreGameTimeStateFromTags();
  void assignStableId(GameTag& tag);
  void resetGameTimeState();

  QVector<GameTag> tags_;
  QString matchNote_;
  quint64 nextTagId_ = 1;
  quint64 tagIdEpoch_ = 1;
  QHash<TagEvent::MainEventKey, int> mainEventCounts_;
  QHash<TagEvent::MainEventKey, QHash<TagEvent::FollowUpChain, int>> followUpCountsByMainEvent_;
  QString homeTeamName_;
  QString awayTeamName_;
  QString homeTeamColor_;
  QString awayTeamColor_;
  QDate gameDate_;
  QString homeAbbrev_;
  QString awayAbbrev_;
  EventLabelOverrides::Table eventLabelOverrides_;
  qint64 gameStartAnchorMs_ = -1;
  int currentQuarterIndex_ = -1;
  qint64 currentQuarterStartMs_ = 0;
  QuarterPhase quarterPhase_ = QuarterPhase::NotStarted;
  std::array<ClosedQuarterSpan, kQuarterCount> closedQuarters_{};
  QVector<GameTimeTagRecord> gameTimeTags_;
};

