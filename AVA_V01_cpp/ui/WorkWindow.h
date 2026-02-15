#pragma once

#include <QWidget>
#include <QtGlobal>
#include <QString>
#include <QHash>
#include <QSet>

class QLabel;
class QAction;
class QToolButton;
class QMenu;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QVBoxLayout;
class QStackedWidget;
class QTimer;
class QDialog;

class VideoPlayer;
class GameControls;
class GameSetupWindow;
class StatsWindow;

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
  void onTagItemActivated(QListWidgetItem* item);
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
  void onQuickFilterPeriodClicked();
  void onQuickFilterTeamClicked();
  void onQuickFilterSituationClicked();
  void onModeToggled();
  void showStatsOverlay();
  void saveNoteDebounceFired();
  void onTeamSetupConfirmed(const QString& filePath,
                            const QString& homeName, const QString& awayName,
                            const QString& homeColor, const QString& awayColor);
  void onTeamSetupCancelled();

private:
  void buildUi();
  void wireSignals();
  void applyTaggingLayout();
  void applyAnalyzingLayout();
  void rebuildTagsList();
  void rebuildFilterMenu();
  void updateFilterIndicator();
  void updateFilterButtonsVisibility();
  void updateTagPlayheadHighlight(qint64 positionMs);
  void syncNoteToSelectedTag();  // immediate save (used on selection change)
  void loadNoteForSelectedTag();
  void flashNewTagRow();
  void clearNewTagFlash();
  bool isMainEventAllowed(const QString& mainEvent) const;
  bool isTagAllowed(const QString& mainEvent, const QString& followUpEvent) const;
  bool isTagAllowedByQuickFilters(const TagSession::GameTag& tag) const;
  bool hasAnyFilterActive() const;
  TagSession::GameTag currentTagContext() const;
  void applyTeamButtonColor(QToolButton* button, const QString& hexColor);

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
  QWidget* tagsSection_ = nullptr;
  QWidget* analyzingMainRow_ = nullptr;
  QWidget* analyzingLeftCol_ = nullptr;
  QWidget* analyzingRightCol_ = nullptr;
  QWidget* contentArea_ = nullptr;
  QVBoxLayout* contentLayout_ = nullptr;
  QToolButton* modeTaggingBtn_ = nullptr;
  QToolButton* modeAnalyzingBtn_ = nullptr;

  // discard or swap video files:
  QToolButton* videoMenuButton_ = nullptr;
  QMenu* videoMenu_ = nullptr;
  QAction* replaceVideoAction_ = nullptr;
  QAction* discardVideoAction_ = nullptr;

  // UI:
  VideoPlayer* videoPlayer_ = nullptr;
  GameControls* gameControls_ = nullptr;
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
  QToolButton* periodQ1_ = nullptr;
  QToolButton* periodQ2_ = nullptr;
  QToolButton* periodQ3_ = nullptr;
  QToolButton* periodQ4_ = nullptr;
  QToolButton* teamHome_ = nullptr;
  QToolButton* teamAway_ = nullptr;
  QToolButton* situationAttacking_ = nullptr;
  QToolButton* situationDefending_ = nullptr;
  QToolButton* undoLastTagButton_ = nullptr;
  QListWidget* tagsList_ = nullptr;

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

  // Quick-filter and tag-context state
  QString quickFilterPeriod_;
  QString quickFilterTeam_;
  QString quickFilterSituation_;
  QString contextPeriod_;
  QString contextTeam_;
  QString contextSituation_;
};
