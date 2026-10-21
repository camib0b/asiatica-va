#pragma once

#include <QDate>
#include <QWidget>
#include <QtGlobal>
#include <QString>
#include <QStringList>
#include <QBrush>
#include <QColor>
#include <QList>
#include <QModelIndex>
#include <QVector>

#include "../export/ExportSettingsDialog.h"
#include "../state/PresentationQueue.h"
#include "TagsTableModel.h"
#include "XmlEventMappingDialog.h"

#include <memory>
#include <optional>

class QLabel;
class QAction;
class QToolButton;
class QTableView;
class QHBoxLayout;
class QVBoxLayout;
class QStackedWidget;
class QSplitter;
class QTimer;
class QDialog;
class QTemporaryDir;

class VideoConcatenator;
class VideoPlayer;
class PlaybackTemporaryDirectoryController;
class NotesController;
class TagsTableController;
class PresentationPlaybackController;
class GameControls;
class TagToastOverlay;
class GameSetupWindow;
class StatsWindow;
class ClipTrimBar;
class PresentationPanel;
class ExportJobsBar;
class ModeToggleTrack;

#include "../state/TagSession.h"

class WorkWindow final : public QWidget {
  Q_OBJECT

public:
  enum class Mode { Tagging, Analyzing, Presenting };

  /// Tagging column on the left: video keeps any width above this cap.
  static constexpr int kMaximumTaggingSidePanelWidthPx = 500;

  /// Whether a source video is loaded. Path, stack index, and widget visibility
  /// are not substitutes for this.
  enum class WorkStage { NoVideo, VideoOpen };

  explicit WorkWindow(QWidget* parent = nullptr);
  ~WorkWindow() override;

  void loadVideoFromFile(const QString& filePath);
  void showTeamSetupForVideo(const QString& filePath, const QStringList& sourceVideoPaths);
  void setTagSession(TagSession* session);
  void setExportDefaultDirectoryFromVideoPath(const QString& videoPath);
  void setConcatenatedVideoTemporaryDirectory(std::unique_ptr<QTemporaryDir> directory);
  void setPendingConcatenation(std::unique_ptr<VideoConcatenator> concatenator);
  void releaseTransientResources();
  Mode mode() const { return mode_; }
  bool hasOpenVideo() const { return workStage_ == WorkStage::VideoOpen; }
  void setMode(Mode mode);

signals:
  void videoClosed();

private slots:
  void onReplaceVideo();
  void onCloseVideo();
  void seekVideoToTagsTableRow(int row);
  void onTagSelectionChanged();
  void onDeleteSelectedTag();
  void onUndoLastTag();
  void onModeToggled();
  void showStatsOverlay();
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
  void onPresentationLeadLagEdited(qint64 leadMs, qint64 lagMs);
  void onPresentationApplyLeadLagToAll(qint64 leadMs, qint64 lagMs);
  void onPresentationShowNotesToggled();
  void onPresentationExportRequested();
  void enforceTaggingSidePanelWidthCap();

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

  void disconnectTagSessionSignals();
  void applySessionTeamIdentity();
  void flashNewTagRow(quint64 tagId);
  void fadeNewTagFlash();
  void syncModeToggleChecks();

  /// Period and team for a tag that is about to be added. Computed from the
  /// session and GameControls; not stored.
  struct PendingTagPeriodAndTeam {
    QString period;
    TagSession::TeamSide team = TagSession::TeamSide::Unspecified;
  };
  PendingTagPeriodAndTeam pendingTagPeriodAndTeam() const;

  struct CapturedMainEvent {
    QString event;
    qint64 markMs = 0;
  };

  /// Wait keeps the video open. Cancel stops the export and drops partial files.
  bool confirmExportAllowsVideoRelease();
  void showXmlImportAdjustmentNotice(int adjustedCount);
  /// Drops concat/prep temps, resets session UI, and emits videoClosed().
  void abortVideoOpen(const QString& errorMessage);

  /// Whether Space and playback-speed keys should control the main video player (same rules for all).
  bool shouldDeliverPlaybackKeyboardToVideoPlayer(const QWidget* focusWidget) const;
  void onApplicationFocusWidgetChanged(QWidget* oldFocus, QWidget* newFocus);
  void refreshPlaybackShortcutFocusGate();
  void updateVideoMuteButton(bool muted) const;
  void setVideoMuteButtonEnabled(bool enabled);
  void flashVideoMuteButton();

  // buildUi pieces. Construction order matches the previous single function.
  QWidget* buildTopToolbar(QWidget* parent);
  void buildModeToggle(QWidget* topRow, QHBoxLayout* topLayout);
  void buildVideoToolbar(QHBoxLayout* topLayout);
  void buildVideoMuteButton(QHBoxLayout* videoControlsLayout);
  void buildVideoSettingsMenu(QHBoxLayout* videoControlsLayout);
  void buildExportJobsBar(QVBoxLayout* rootLayout);
  void buildTaggingSideColumn();
  void buildTagsSection();
  void buildTagsHeaderRow(QVBoxLayout* tagsSectionLayout);
  void buildTagsFilterMenu();
  void buildTagsTable(QVBoxLayout* tagsSectionLayout);
  void buildGameControlsAndStatsWindow();
  void buildNotesColumn();
  void buildVideoStageColumn();
  void buildPresentationBanner(QVBoxLayout* videoColumnLayout);
  void buildWorkSideStack();
  void assembleWorkSplitters(QVBoxLayout* contentAreaLayout);
  void hideChromeUntilVideoOpens();

  // wireSignals pieces. Connection order matches the previous single function.
  void wireGameSetupSignals();
  void wireVideoChromeSignals();
  void wireGameControlsSignals();
  void wireTagsTableSignals();
  void wireModeToggleSignals();
  void wireNotesSignals();
  void wireTagsTableActionSignals();
  void installModeAndStatsShortcuts();
  void wireApplicationLanguageSignals();
  void wirePlaybackAndTemporaryDirectorySignals();
  void installTagEditShortcuts();
  void wireApplicationFocusSignals();
  void wirePresentationPanelSignals();
  void rememberCapturedMainEvent(const QString& mainEvent);
  void showTagSequenceStep(const QString& shortcutKey,
                           const QString& stepLabel,
                           const QColor& teamColor,
                           bool chainOpen,
                           bool startsChain);
  void dismissTagSequenceToast();
  void commitTagFromGameControls(const QString& mainEvent, const QString& followUpEvent);
  void seekVideoFromTagsTableIndex(const QModelIndex& index);
  void onTagsTableCurrentChanged(const QModelIndex& current, const QModelIndex& previous);
  void toggleVideoMuteFromToolbar();
  void cycleWorkMode();
  void onVideoPositionChanged(qint64 positionMs);
  void onVideoDurationChanged(qint64 durationMs);

  // Mode chrome. setMode and applyModeChrome stay the entry points.
  void updatePresentationChromeVisibility(bool isPresenting);
  void placeNotesColumnForMode(bool isTagging, bool isAnalyzing);
  void showModeSidePage(bool isTagging, bool isAnalyzing, bool isPresenting);
  void applyModeSizePolicies(bool isTagging, bool isAnalyzing, bool isPresenting);
  void placeSidePanelForMode(bool isTagging);
  void updateTagToastForMode(bool isTagging);
  void setModeToggleButtonsChecked();
  /// Video close or load while Presenting. Assigns Tagging directly, detaches the Tab
  /// filter, then applies chrome. setMode would flush notes and commit ModeExit first.
  void leavePresentingForVideoChange();

  void bindTagSessionToSurfaces();
  void connectTagSessionSignals();
  void onTagSessionTagsChanged(quint64 addedTagId);
  void onTagSessionTagsImported();
  void onTagSessionGameMetadataChanged();

  std::optional<QString> preparePlaybackPath(const QString& filePath);
  void resetSessionForOpenedVideo();
  void showPlayerForOpenedVideo(const QString& playbackPath);
  void showWorkChromeForOpenedVideo();
  void resetPresentationForOpenedVideo();
  void refreshSurfacesForOpenedVideo();
  void replaceOpenVideoWithFile(const QString& filePath);
  void replaceOpenVideoWithConcatenation(QStringList filePaths);

  QString suggestedXmlReportPath() const;
  bool startXmlReportJob(const QString& outputPath,
                         const QString& failureTitle,
                         const QString& emptyErrorFallback);
  bool writeSessionNotesBesideXml(const QString& xmlPath);
  QVector<PresentationQueue::Clip> copyPresentationQueueClips() const;
  void appendRetainedPlaybackDirectories(ExportJobRequest& request) const;
  ExportJobRequest buildPresentationExportRequest(
      const ExportSettingsDialog::Result& settings,
      const QVector<PresentationQueue::Clip>& queuedClips) const;

  std::optional<TagSession::ImportMode> promptXmlImportMode();
  std::optional<qint64> promptXmlSyncOffset(
      const XmlImporter::SyncAnchorResult& anchor,
      const QVector<XmlImporter::ParsedInstance>& instances);
  std::optional<XmlEventMappingDialog::ImportMappingResult> promptXmlEventMapping(
      const QVector<XmlImporter::ParsedInstance>& instances,
      qint64 offsetMs);
  void importMappedXmlTags(TagSession::ImportMode importMode,
                           const XmlEventMappingDialog::ImportMappingResult& importResult);

  // Mode and layout
  Mode mode_ = Mode::Tagging;
  WorkStage workStage_ = WorkStage::NoVideo;
  QStackedWidget* contentStack_ = nullptr;
  QWidget* mainContentContainer_ = nullptr;
  GameSetupWindow* gameSetupWidget_ = nullptr;
  QWidget* taggingSideColumn_ = nullptr;
  QWidget* tagsSection_ = nullptr;
  QWidget* tagsHeaderRow_ = nullptr;
  QSplitter* workOuterSplitter_ = nullptr;
  QSplitter* videoAndTagsSplitter_ = nullptr;
  QWidget* videoColumn_ = nullptr;
  QSplitter* workTagsNotesSplitter_ = nullptr;
  QStackedWidget* workSideStack_ = nullptr;
  QToolButton* modeTaggingButton_ = nullptr;
  QToolButton* modeAnalyzingButton_ = nullptr;
  QToolButton* modePresentingButton_ = nullptr;
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
  TagToastOverlay* tagToast_ = nullptr;
  GameControls* gameControls_ = nullptr;
  StatsWindow* statsWindow_ = nullptr;
  QWidget* notesColumn_ = nullptr;
  QDialog* statsOverlayDialog_ = nullptr;
  StatsWindow* statsOverlay_ = nullptr;
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

  std::optional<CapturedMainEvent> capturedMainEvent_;

  QString sourceVideoPath_;
  QString exportDefaultDirectoryPath_;
  /// Concat, playback-prep, and export-retained temporary directories.
  std::unique_ptr<PlaybackTemporaryDirectoryController> playbackTemporaryDirectoryController_;
  /// Clip-note and match-note editors, debounce timers, and the unsaved clip draft.
  std::unique_ptr<NotesController> notesController_;
  /// Tags-table rebuild, event filter, and playhead highlight.
  std::unique_ptr<TagsTableController> tagsTableController_;
  /// Presentation playback state, queue signals, clip bar, Tab shortcuts, and stage text.
  std::unique_ptr<PresentationPlaybackController> presentationPlaybackController_;
};
