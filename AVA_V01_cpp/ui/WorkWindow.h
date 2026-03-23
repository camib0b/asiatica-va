#pragma once

#include <QWidget>
#include <QtGlobal>
#include <QString>
#include <QBrush>
#include <QHash>
#include <QSet>

class QLabel;
class QAction;
class QToolButton;
class QMenu;
class QTableWidget;
class QTableWidgetItem;
class QPlainTextEdit;
class QVBoxLayout;
class QStackedWidget;
class QSplitter;
class QTimer;
class QDialog;

class VideoPlayer;
class GameControls;
class GameSetupWindow;
class StatsWindow;
class Scoreboard;

#include "../state/TagSession.h"

class WorkWindow final : public QWidget {
  Q_OBJECT

public:
  enum class Mode { Tagging, Analyzing };

  explicit WorkWindow(QWidget* parent = nullptr);
  ~WorkWindow() override = default;

  void loadVideoFromFile(const QString& filePath);
  void showTeamSetupForVideo(const QString& filePath);
  void setTagSession(TagSession* session);
  Mode mode() const { return mode_; }
  void setMode(Mode m);

signals:
  void videoClosed();

private slots:
  void onReplaceVideo();
  void onDiscardVideo();
  void onTagTableSeekToRow(int row);
  void onTagSelectionChanged();
  void onNoteTextChanged();
  void onDeleteSelectedTag();
  void onUndoLastTag();
  void onSelectAllFilters();
  void onSelectNoFilters();
  void onFilterActionToggled(bool checked);
  void onPlayheadPositionChanged(qint64 positionMs);
  void onFilterByPathRequested(const QString& mainEvent, const QString& followUpEvent);
  void onRemoveFilters();
  void onModeToggled();
  void showStatsOverlay();
  void saveNoteDebounceFired();
  void onTeamSetupConfirmed(const QString& filePath,
                            const QString& homeName, const QString& awayName,
                            const QString& homeColor, const QString& awayColor);
  void onTeamSetupCancelled();
  void onExportClips();
  void onApplicationLanguageChanged();

private:
  void buildUi();
  void wireSignals();
  void applyUiStrings();
  void applyTaggingLayout();
  void applyAnalyzingLayout();
  void applyAnalyzingSplitterGeometry();
  void applyTaggingSplitterGeometry();
  void rebuildTagsList();
  void rebuildFilterMenu();
  void updateFilterIndicator();
  void updateFilterButtonsVisibility();
  void updateTagPlayheadHighlight(qint64 positionMs);
  void syncNoteToSelectedTag();  // immediate save (used on selection change)
  void loadNoteForSelectedTag();
  void flashNewTagRow();
  void clearNewTagFlash();
  QTableWidgetItem* currentTagKeyItem() const;
  void setTagTableRowBackground(int row, const QBrush& brush);
  QString displayTeamForTag(const TagSession::GameTag& tag) const;
  bool isMainEventAllowed(const QString& mainEvent) const;
  bool isTagAllowed(const QString& mainEvent, const QString& followUpEvent) const;
  bool isTagAllowedByQuickFilters(const TagSession::GameTag& tag) const;
  bool hasAnyFilterActive() const;
  TagSession::GameTag currentTagContext() const;

  QString promptForVideoFile();

  // Mode and layout
  Mode mode_ = Mode::Tagging;
  QStackedWidget* contentStack_ = nullptr;
  QWidget* mainContentContainer_ = nullptr;
  GameSetupWindow* gameSetupWidget_ = nullptr;
  QWidget* videoControlsRow_ = nullptr;
  QWidget* videoTimelineRow_ = nullptr;
  QWidget* taggingMainRow_ = nullptr;
  QWidget* taggingVideoCol_ = nullptr;
  QWidget* taggingRightCol_ = nullptr;
  QSplitter* taggingVideoTagsSplitter_ = nullptr;
  QWidget* tagsSection_ = nullptr;
  QWidget* tagsHeaderRow_ = nullptr;
  QSplitter* analyzingMainSplitter_ = nullptr;
  QSplitter* analyzingLeftSplitter_ = nullptr;
  QSplitter* analyzingRightSplitter_ = nullptr;
  QSplitter* analyzingTagsControlsSplitter_ = nullptr;
  QWidget* contentArea_ = nullptr;
  QVBoxLayout* contentLayout_ = nullptr;
  QToolButton* modeTaggingBtn_ = nullptr;
  QToolButton* modeAnalyzingBtn_ = nullptr;

  // discard or swap video files:
  QToolButton* videoMenuButton_ = nullptr;
  QMenu* videoMenu_ = nullptr;
  QAction* replaceVideoAction_ = nullptr;
  QAction* discardVideoAction_ = nullptr;
  QAction* exportClipsAction_ = nullptr;
  QAction* statsOverlayAction_ = nullptr;

  // UI:
  VideoPlayer* videoPlayer_ = nullptr;
  GameControls* gameControls_ = nullptr;
  Scoreboard* scoreboard_ = nullptr;
  StatsWindow* statsWindow_ = nullptr;
  QPlainTextEdit* notesEdit_ = nullptr;
  QDialog* statsOverlayDialog_ = nullptr;
  StatsWindow* statsOverlay_ = nullptr;
  QTimer* noteDebounceTimer_ = nullptr;
  int pendingNoteIndex_ = -1;
  QString pendingNoteText_;

  QLabel* tagsHeaderLabel_ = nullptr;
  QToolButton* tagsFilterButton_ = nullptr;
  QToolButton* tagsRemoveFiltersButton_ = nullptr;
  QMenu* tagsFilterMenu_ = nullptr;
  QLabel* tagsFilterIndicator_ = nullptr;
  QToolButton* undoLastTagButton_ = nullptr;
  QTableWidget* tagsTable_ = nullptr;

  QTimer* newTagFlashTimer_ = nullptr;
  int newTagFlashRow_ = -1;

  TagSession* tagSession_ = nullptr;
  QHash<QString, QAction*> filterActionByMainEvent_;
  QSet<QString> allowedMainEvents_;
  QString activeFilterPathMainEvent_;
  QString activeFilterPathFollowUp_;

  QString pendingMainEvent_;
  qint64 pendingTimestampMs_ = 0;
  bool hasPendingTag_ = false;

  // Tag-context state (period/team/situation for new tags)
  QString contextPeriod_;
  QString contextTeam_;
  QString contextSituation_;

  QString sourceVideoPath_;
};
