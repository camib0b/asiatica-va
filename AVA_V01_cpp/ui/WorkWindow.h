#pragma once

#include <QDate>
#include <QWidget>
#include <QtGlobal>
#include <QString>
#include <QStringList>
#include <QBrush>
#include <QHash>
#include <QList>
#include <QVector>

#include <memory>

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
class QTemporaryDir;

class VideoConcatenator;
class VideoPlayer;
class GameControls;
class GameSetupWindow;
class StatsWindow;
class ClipTrimBar;
class PresentationPanel;
class PresentationQueue;
class ExportJobManager;
class ExportJobsBar;
class MatchNotesEditor;

#include "../state/TagSession.h"

class WorkWindow final : public QWidget {
  Q_OBJECT

public:
  enum class Mode { Tagging, Analyzing, Presenting };

  explicit WorkWindow(QWidget* parent = nullptr);
  ~WorkWindow() override;

  void loadVideoFromFile(const QString& filePath);
  void showTeamSetupForVideo(const QString& filePath, const QStringList& sourceVideoPaths);
  void setTagSession(TagSession* session);
  void setExportDefaultDirectoryFromVideoPath(const QString& videoPath);
  void setConcatenatedVideoTempDir(std::unique_ptr<QTemporaryDir> dir);
  void setPendingConcatenation(std::unique_ptr<VideoConcatenator> concatenator);
  void releaseTransientResources();
  Mode mode() const { return mode_; }
  void setMode(Mode m);

signals:
  void videoClosed();

private slots:
  void onReplaceVideo();
  void onCloseVideo();
  void onTagTableSeekToRow(int row);
  void onTagSelectionChanged();
  void onNoteTextChanged();
  void onMatchNoteTextChanged();
  void onMatchNoteTagMentionActivated(quint64 tagId);
  void onDeleteSelectedTag();
  void onUndoLastTag();
  void onSelectAllFilters();
  void onSelectNoFilters();
  void onFilterActionToggled(bool checked);
  void onPlayheadPositionChanged(qint64 positionMs);
  void onFilterByEventPathRequested(const QString& mainEvent, const QString& followUpEvent);
  void onRemoveFilters();
  void onModeToggled();
  void showStatsOverlay();
  void saveNoteDebounceFired();
  void saveMatchNoteDebounceFired();
  void onGameSetupConfirmed(const QString& filePath,
                            const QString& homeName, const QString& awayName,
                            const QString& homeColor, const QString& awayColor,
                            const QString& competitionName,
                            const QDate& gameDate,
                            const QString& homeAbbrev,
                            const QString& awayAbbrev);
  void onGameSetupCancelled();
  void onGameStartRequested();
  void onNextQuarterRequested();
  void onClipDurationSettings();
  void onImportXml();
  void onApplicationLanguageChanged();

  // Presentation mode
  void onPresentationSelectionChanged(const QVector<int>& tagSessionIndexes);
  void onPresentationClipActivated(int tagSessionIndex);
  void onPresentationCurrentClipChanged(int queueIndex);
  void onPresentationQueueChanged();
  void onPresentationClipIntervalChanged(int queueIndex);
  void onPresentationLeadLagEdited(qint64 leadMs, qint64 lagMs);
  void onPresentationApplyLeadLagToAll(qint64 leadMs, qint64 lagMs);
  void onPresentationShowNotesToggled(bool enabled);
  void onPresentationExportRequested();

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  void buildUi();
  void buildPresentationUi();
  void wireSignals();
  void applyUiStrings() const;
  void applyTaggingLayout();
  void applyAnalyzingLayout();
  void applyPresentationLayout();
  void detachWidgetFromParent(QWidget* widget);
  void applyAnalyzingSplitterGeometry();
  void applyTaggingSplitterGeometry();
  void applyPresentationSplitterGeometry();

  /// Cycles Tagging → Analyzing → Presenting → Tagging (the M shortcut).
  static constexpr Mode nextModeInCycle(Mode current) {
    switch (current) {
      case Mode::Tagging: return Mode::Analyzing;
      case Mode::Analyzing: return Mode::Presenting;
      case Mode::Presenting: return Mode::Tagging;
    }
    return Mode::Tagging;
  }

  // Presentation mode helpers
  void showPresentationClip(int queueIndex, bool startPlaying);
  void goToNextPresentationClip();
  void goToPreviousPresentationClip();
  void updatePresentationStage() const;
  void configurePresentationClipBarForCurrentClip() const;
  void savePresentationClipIntervalFromClipBar();
  void updatePresentationPlayhead(qint64 positionMs);
  void armPresentationAutoPause(qint64 clipEndMs);
  void attachPresentationKeyboardShortcuts();
  void detachPresentationKeyboardShortcuts();
  void captureTaggingModeUiStateForRestore();
  void restoreTaggingModeUiStateAfterLayout();
  void disconnectTagSessionSignals();
  void syncContextPeriodFromSession();
  void rebuildTagsList();
  void rebuildFilterMenu();
  void updateFilterIndicator() const;
  void updateFilterButtonsVisibility() const;
  void updateTagPlayheadHighlight(qint64 positionMs) const;
  void flushPendingClipNote();
  void discardPendingClipNote();
  quint64 selectedTagId() const;
  void loadNoteForSelectedTag() const;
  void loadMatchNote() const;
  void refreshMatchNoteMentionCandidates() const;
  void flashNewTagRow();
  void clearNewTagFlash();
  QTableWidgetItem* selectedTagRowTimeItem() const;
  void setTagTableRowBackground(int row, const QBrush& brush) const;
  QString displayTeamForTag(const TagSession::GameTag& tag) const;
  bool isMainEventAllowed(const QString& mainEvent) const;
  bool isTagAllowed(const QString& mainEvent, const QString& followUpEvent) const;
  bool hasAnyFilterActive() const;
  TagSession::GameTag pendingTagPeriodAndTeam() const;

  void cleanupConcatenatedVideo();
  void cleanupPlaybackPrepVideo();
  void cleanupPendingConcatenation();
  /// Drops concat/prep temps, resets session UI, and emits videoClosed().
  void abortVideoOpen(const QString& errorMessage);

  /// Whether Space and playback-speed keys should control the main video player (same rules for all).
  bool shouldDeliverPlaybackKeyboardToVideoPlayer(const QWidget* focusWidget) const;
  void onApplicationFocusWidgetChanged(QWidget* oldFocus, QWidget* newFocus) const;
  void refreshPlaybackShortcutFocusGate() const;

  // Mode and layout
  Mode mode_ = Mode::Tagging;
  QStackedWidget* contentStack_ = nullptr;
  QWidget* mainContentContainer_ = nullptr;
  /// Hidden parent for widgets taken out of a layout or splitter so they never become
  /// top-level windows and stay in this object's tree until reinserted.
  QWidget* detachedWidgetHost_ = nullptr;
  GameSetupWindow* gameSetupWidget_ = nullptr;
  QWidget* videoControlsRow_ = nullptr;
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
  QToolButton* modePresentingBtn_ = nullptr;

  // Presentation mode
  QSplitter* presentationSplitter_ = nullptr;
  QWidget* presentationStageColumn_ = nullptr;
  QWidget* presentationBanner_ = nullptr;
  QLabel* presentationEventLabel_ = nullptr;
  QLabel* presentationContextLabel_ = nullptr;
  QLabel* presentationNoteLabel_ = nullptr;
  ClipTrimBar* presentationClipBar_ = nullptr;
  PresentationPanel* presentationPanel_ = nullptr;
  PresentationQueue* presentationQueue_ = nullptr;
  ExportJobManager* exportJobManager_ = nullptr;
  ExportJobsBar* exportJobsBar_ = nullptr;
  bool presentationKeyboardShortcutsInstalled_ = false;
  bool presentationAutoPauseArmed_ = false;
  bool updatingPresentationClipBar_ = false;
  /// False until the first clip of the queue has been played, so the very first Tab starts the
  /// queue instead of skipping past its first clip.
  bool presentationPlaybackStarted_ = false;
  qint64 presentationAutoPauseAtMs_ = 0;
  qint64 lastPresentationPlayheadMs_ = -1;

  // close or swap video files:
  QToolButton* videoMenuButton_ = nullptr;
  QMenu* videoMenu_ = nullptr;
  QAction* replaceVideoAction_ = nullptr;
  QAction* closeVideoAction_ = nullptr;
  QAction* importXmlAction_ = nullptr;
  QAction* clipDurationSettingsAction_ = nullptr;
  QAction* statsOverlayAction_ = nullptr;

  // UI:
  VideoPlayer* videoPlayer_ = nullptr;
  GameControls* gameControls_ = nullptr;
  StatsWindow* statsWindow_ = nullptr;
  QWidget* notesColumn_ = nullptr;
  QLabel* matchNotesLabel_ = nullptr;
  MatchNotesEditor* matchNotesEditor_ = nullptr;
  QLabel* clipNotesLabel_ = nullptr;
  QPlainTextEdit* notesEdit_ = nullptr;
  QDialog* statsOverlayDialog_ = nullptr;
  StatsWindow* statsOverlay_ = nullptr;
  QTimer* noteDebounceTimer_ = nullptr;
  QTimer* matchNoteDebounceTimer_ = nullptr;
  quint64 pendingNoteTagId_ = 0;
  QString pendingNoteText_;
  bool suppressClipNoteReload_ = false;

  QLabel* tagsHeaderLabel_ = nullptr;
  QToolButton* tagsFilterButton_ = nullptr;
  QToolButton* tagsRemoveFiltersButton_ = nullptr;
  QMenu* tagsFilterMenu_ = nullptr;
  QLabel* tagsFilterIndicator_ = nullptr;
  QToolButton* undoLastTagButton_ = nullptr;
  QTableWidget* tagsTable_ = nullptr;

  QTimer* newTagFlashTimer_ = nullptr;
  int newTagFlashRow_ = -1;
  QTimer* playheadSideEffectsDebounceTimer_ = nullptr;
  qint64 lastPlayheadPositionForSideEffectsMs_ = 0;

  TagSession* tagSession_ = nullptr;
  QVector<QMetaObject::Connection> tagSessionConnections_;
  QHash<QString, QAction*> filterActionByMainEvent_;
  QString activeEventPathMainEvent_;
  QString activeEventPathFollowUp_;

  QString pendingMainEvent_;
  qint64 pendingTimestampMs_ = 0;
  bool hasPendingTag_ = false;

  // Tag-context state (period/team for new tags)
  QString contextPeriod_;
  QString contextTeam_;

  QString sourceVideoPath_;
  QString playbackVideoPath_;
  QString exportDefaultDirectoryPath_;
  std::unique_ptr<QTemporaryDir> concatenatedVideoTempDir_;
  std::unique_ptr<QTemporaryDir> playbackPrepTempDir_;
  std::unique_ptr<VideoConcatenator> pendingConcatenator_;

  QList<int> preservedTaggingVideoTagsSplitterSizes_;
  int preservedTagsTableVerticalScrollValue_ = 0;
  int preservedTagsTableHorizontalScrollValue_ = 0;
  bool hasPreservedTaggingUiState_ = false;
};
