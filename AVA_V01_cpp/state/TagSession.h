#pragma once

#include <QDate>
#include <QObject>
#include <QHash>
#include <QString>
#include <QVector>
#include <QtGlobal>

class TagSession final : public QObject {
  Q_OBJECT

public:
  struct GameTag {
    QString mainEvent;
    QString followUpEvent;
    qint64 positionMs = 0;  // Click moment: the anchor used for table seek/highlight.
    qint64 startMs = 0;     // Clip interval start (used by exporters).
    qint64 endMs = 0;       // Clip interval end (used by exporters).
    QString note;
    QString period;   // e.g. "Q1", "Q2", "Q3", "Q4"
    QString team;    // e.g. "Home", "Away"
    QString situation; // e.g. "Attacking", "Defending"
    bool intervalManuallyEdited = false; // true when the user trimmed start/end away from defaults
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

  struct ImportResult {
    int importedCount = 0;
    int skippedCount = 0;
    int clampedCount = 0;
  };

  explicit TagSession(QObject* parent = nullptr);
  ~TagSession() override = default;

  void clear();
  void clearTeamInfo();
  void setGameTeams(const QString& homeName, const QString& awayName,
                   const QString& homeColor, const QString& awayColor);
  /// Updates competition / date / abbreviations metadata used by the XML exporter.
  /// Empty strings clear the corresponding field.
  void setGameMetadata(const QString& competitionName,
                       const QDate& gameDate,
                       const QString& homeAbbrev,
                       const QString& awayAbbrev);

  QString homeTeamName() const { return homeTeamName_; }
  QString awayTeamName() const { return awayTeamName_; }
  QString homeTeamColor() const { return homeTeamColor_; }
  QString awayTeamColor() const { return awayTeamColor_; }
  QString competitionName() const { return competitionName_; }
  QDate gameDate() const { return gameDate_; }
  int gameYear() const { return gameDate_.isValid() ? gameDate_.year() : 0; }
  QString homeAbbrev() const { return homeAbbrev_; }
  QString awayAbbrev() const { return awayAbbrev_; }

  void addTag(const GameTag& tag);
  ImportResult importTags(const QVector<GameTag>& tags,
                          ImportMode mode,
                          qint64 videoDurationMs = -1);
  void removeTag(int index);
  void setTagNote(int index, const QString& note);
  QString tagNote(int index) const;
  /// Updates the clip interval (start/end in ms) of the tag at \p index.
  /// When \p userInitiated is true (export trim bar drag), the tag is always marked as
  /// manually trimmed so later default-duration changes cannot overwrite it.
  void setTagInterval(int index, qint64 startMs, qint64 endMs, bool userInitiated = false);
  /// Re-applies pre/post defaults (in ms) to every tag of \p mainEvent that has not been
  /// manually trimmed. Quarter / start-anchor tags are skipped because their interval is
  /// determined by user clicks, not by symmetric pads.
  void applyDefaultsToUntrimmedTags(const QString& mainEvent, qint64 preMs, qint64 postMs);

  // ---- Game-time anchor & quarter tracking ----
  qint64 gameStartAnchorMs() const { return gameStartAnchorMs_; }
  bool hasGameStartAnchor() const { return gameStartAnchorMs_ >= 0; }
  void setGameStartAnchor(qint64 positionMs) { gameStartAnchorMs_ = positionMs; }

  QuarterPhase quarterPhase() const { return quarterPhase_; }
  /// Index of the quarter currently in progress (0=Q1 .. 3=Q4); -1 when not in progress.
  int currentQuarterIndex() const { return currentQuarterIndex_; }
  /// Playhead position when the in-progress quarter started.
  qint64 currentQuarterStartMs() const { return currentQuarterStartMs_; }
  void setQuarterPhase(QuarterPhase phase) { quarterPhase_ = phase; }
  void setCurrentQuarter(int index, qint64 startMs);
  void clearCurrentQuarter();
  void resetGameTimeState();

  /// Quarter label from TimeCodes tags (Q1–Q4) and live quarter state; empty if unknown.
  QString periodLabelAtTimestampMs(qint64 positionMs) const;

  const QVector<GameTag>& tags() const { return tags_; }
  const QHash<QString, int>& mainEventCounts() const { return mainEventCounts_; }
  const QHash<QString, QHash<QString, int>>& followUpCountsByMainEvent() const { return followUpCountsByMainEvent_; }

signals:
  void cleared();
  void tagAdded(const TagSession::GameTag& tag);
  void tagsImported();
  void tagNoteChanged(int index);
  void tagIntervalChanged(int index);
  void statsChanged();

private:
  void rebuildStatsFromTags();
  void restoreGameTimeStateFromTags();
  QVector<GameTag> tags_;
  QHash<QString, int> mainEventCounts_;
  QHash<QString, QHash<QString, int>> followUpCountsByMainEvent_;
  QString homeTeamName_;
  QString awayTeamName_;
  QString homeTeamColor_;
  QString awayTeamColor_;
  QString competitionName_;
  QDate gameDate_;
  QString homeAbbrev_;
  QString awayAbbrev_;
  qint64 gameStartAnchorMs_ = -1;
  int currentQuarterIndex_ = -1;
  qint64 currentQuarterStartMs_ = 0;
  QuarterPhase quarterPhase_ = QuarterPhase::NotStarted;
};

