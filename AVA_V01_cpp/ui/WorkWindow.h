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

#include "../state/PendingClipNote.h"
#include "../state/TagFilter.h"

#include <memory>
#include <optional>

class QLabel;
class QAction;
class QToolButton;
class QTableView;
class TagsTableModel;
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
class ModeToggleTrack;

#include "../state/PresentationPlayback.h"
#include "../state/TagSession.h"

class WorkWindow final : public QWidget {
  Q_OBJECT

public:
  enum class Mode { Tagging, Analyzing, Presenting };

  /// Whether a source video is loaded. Path, stack index, and widget visibility
  /// are not substitutes for this.
  enum class WorkStage { NoVideo, VideoOpen };

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
  bool hasOpenVideo() const { return workStage_ == WorkStage::VideoOpen; }
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
                            const QDate& gameDate,
                            const QString& homeAbbrev,
                            const QString& awayAbbrev);
  void onGameSetupCancelled();
  void onGameStartRequested();
  void onNextQuarterRequested();
  void onClipDurationSettings();
  void onImportXml();
  void onExportXml();
  void onApplicationLanguageChanged();

  // Presentation mode
  void onPresentationSelectionChanged(const QVector<int>& tagSessionIndexes);
  void onPresentationClipActivated(int tagSessionIndex);
  void onPresentationCurrentClipChanged(int queueIndex);
  void onPresentationQueueChanged();
  void onPresentationClipIntervalChanged(int queueIndex);
  void onPresentationLeadLagEdited(qint64 leadMs, qint64 lagMs);
  void onPresentationApplyLeadLagToAll(qint64 leadMs, qint64 lagMs);
  void onPresentationShowNotesToggled();
  void onPresentationExportRequested();

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  bool exportXmlAndNotesBeforeReplace();
  void buildUi();
  void buildPresentationUi();
  void wireSignals();
  void applyUiStrings() const;
  void applyModeChrome();
  void applySidePanelCompressedWidth();

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
  void showPresentationClip(int queueIndex);
  void goToNextPresentationClip();
  void goToPreviousPresentationClip();
  void stepPresentationQueue(bool forward);
  void commitPresentationPlayback(const PresentationPlayback::Result& result);
  void resetPresentationForVideoChange();
  void updatePresentationStage() const;
  void configurePresentationClipBarForCurrentClip() const;
  void savePresentationClipIntervalFromClipBar();
  void updatePresentationPlayhead(qint64 positionMs);
  void attachPresentationKeyboardShortcuts();
  void detachPresentationKeyboardShortcuts();
  void disconnectTagSessionSignals();
  void refreshTagsTableRows();
  void syncFilterMenu();
  void updateFilterIndicator() const;
  void updateFilterButtonsVisibility() const;
  void applySessionTeamIdentity();
  void flushPendingClipNote();
  void discardPendingClipNote();
  void stopNoteTimers();
  void resumeNoteTimers();
  quint64 selectedTagId() const;
  void loadNoteForSelectedTag() const;
  void loadMatchNote() const;
  void refreshMatchNoteMentionCandidates() const;
  void flashNewTagRow(quint64 tagId);
  void fadeNewTagFlash();
  void syncModeToggleChecks();
  QString displayTeamForTag(const TagSession::GameTag& tag) const;
  bool isTagAllowed(const QString& mainEvent, const QString& followUpEvent) const;
  bool hasAnyFilterActive() const;

  /// Period and team for a tag that is about to be added. Computed from the
  /// session and GameControls; not stored.
  struct TagContext {
    QString period;
    TagSession::TeamSide team = TagSession::TeamSide::Unspecified;
  };
  TagContext tagContext() const;

  struct CapturedMainEvent {
    QString event;
    qint64 positionMs = 0;
  };

  void cleanupConcatenatedVideo();
  void cleanupPlaybackPrepVideo();
  void cleanupPendingConcatenation();
  /// Drops concat/prep temps, resets session UI, and emits videoClosed().
  void abortVideoOpen(const QString& errorMessage);

  /// Whether Space and playback-speed keys should control the main video player (same rules for all).
  bool shouldDeliverPlaybackKeyboardToVideoPlayer(const QWidget* focusWidget) const;
  void onApplicationFocusWidgetChanged(QWidget* oldFocus, QWidget* newFocus) const;
  void refreshPlaybackShortcutFocusGate() const;
  void updateVideoMuteButton(bool muted) const;
  void setVideoMuteButtonEnabled(bool enabled) const;
  void flashVideoMuteButton();

  // Mode and layout
  Mode mode_ = Mode::Tagging;
  WorkStage workStage_ = WorkStage::NoVideo;
  QStackedWidget* contentStack_ = nullptr;
  QWidget* mainContentContainer_ = nullptr;
  GameSetupWindow* gameSetupWidget_ = nullptr;
  QWidget* taggingRightCol_ = nullptr;
  QWidget* tagsSection_ = nullptr;
  QWidget* tagsHeaderRow_ = nullptr;
  QSplitter* workOuterSplitter_ = nullptr;
  QSplitter* workLeftSplitter_ = nullptr;
  QWidget* videoColumn_ = nullptr;
  QSplitter* workTagsNotesSplitter_ = nullptr;
  QStackedWidget* workSideStack_ = nullptr;
  QToolButton* modeTaggingBtn_ = nullptr;
  QToolButton* modeAnalyzingBtn_ = nullptr;
  QToolButton* modePresentingBtn_ = nullptr;
  ModeToggleTrack* modeToggleTrack_ = nullptr;

  // Presentation mode
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
  bool updatingPresentationClipBar_ = false;
  PresentationPlayback::State presentationPlayback_{};

  // close or swap video files:
  QToolButton* videoMuteButton_ = nullptr;
  QToolButton* videoMenuButton_ = nullptr;
  QAction* replaceVideoAction_ = nullptr;
  QAction* closeVideoAction_ = nullptr;
  QAction* importXmlAction_ = nullptr;
  QAction* exportXmlAction_ = nullptr;
  QAction* clipDurationSettingsAction_ = nullptr;
  QAction* reduceMotionAction_ = nullptr;
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
  bool resumeClipNoteTimer_ = false;
  bool resumeMatchNoteTimer_ = false;
  std::optional<PendingClipNote> pendingClipNote_;
  QLabel* tagsHeaderLabel_ = nullptr;
  QToolButton* tagsFilterButton_ = nullptr;
  QToolButton* tagsRemoveFiltersButton_ = nullptr;
  QMenu* tagsFilterMenu_ = nullptr;
  QLabel* tagsFilterIndicator_ = nullptr;
  QToolButton* undoLastTagButton_ = nullptr;
  QTableView* tagsTable_ = nullptr;
  TagsTableModel* tagsModel_ = nullptr;

  QTimer* newTagFlashTimer_ = nullptr;

  TagSession* tagSession_ = nullptr;
  QVector<QMetaObject::Connection> tagSessionConnections_;
  QAction* filterSelectAllAction_ = nullptr;
  QAction* filterSelectNoneAction_ = nullptr;
  QHash<QString, QAction*> filterActionByMainEvent_;
  TagFilter tagFilter_;

  std::optional<CapturedMainEvent> capturedMainEvent_;

  QString sourceVideoPath_;
  QString exportDefaultDirectoryPath_;
  std::unique_ptr<QTemporaryDir> concatenatedVideoTempDir_;
  std::unique_ptr<QTemporaryDir> playbackPrepTempDir_;
  std::unique_ptr<VideoConcatenator> pendingConcatenator_;
};
