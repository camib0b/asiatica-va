#pragma once

#include "../state/TagFilter.h"
#include "../state/TagSession.h"
#include "TagsTableModel.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>

class QAction;
class QLabel;
class QMenu;
class QTableView;
class QToolButton;

class NotesController;
class VideoPlayer;

/// Owns the tags-table rebuild, the event filter, and playhead highlighting.
///
/// WorkWindow still builds the table, model, filter menu, indicator, and
/// remove-filters button, then hands them over. A nested tagsChanged during a
/// rebuild sets tagsTableRebuildRequested_ and returns; the ScopedReentrancyFlag
/// around rebuildingTagsTable_ runs that queued rebuild once the in-flight pass
/// finishes. Table and selection signals stay blocked through the note reload so
/// the rebuild cannot re-enter or save the open clip draft.
class TagsTableController final : public QObject {
  Q_OBJECT

public:
  explicit TagsTableController(QObject* parent = nullptr);
  ~TagsTableController() override;

  void setTagSession(TagSession* tagSession);
  void setNotesController(NotesController* notesController);
  void setVideoPlayer(VideoPlayer* videoPlayer);
  void setTagsTable(QTableView* tagsTable, TagsTableModel* tagsModel);
  void setFilterWidgets(QMenu* filterMenu,
                        QAction* selectAllAction,
                        QAction* selectNoneAction,
                        QLabel* filterIndicator,
                        QToolButton* removeFiltersButton);

  /// Select-all, select-none, menu hide, and remove-filters. Per-event actions connect as they are added.
  void wireFilterSignals();

  void syncFilterMenu();
  void refreshTagsTableRows();
  void updatePlayheadHighlight(qint64 positionMs);
  void onFilterByEventPathRequested(const TagEvent::MainEventKey& mainEvent,
                                   const TagEvent::FollowUpChain& followUps);

private:
  void onSelectAllFilters();
  void onSelectNoneFilters();
  void onFilterActionToggled(bool checked);
  void onRemoveFilters();
  void onTagsFilterMenuAboutToHide();
  void applyDeferredFilterMenuStructureSync();
  bool tagPassesEventFilter(const TagEvent::MainEventKey& mainEvent,
                            const TagEvent::FollowUpChain& followUps) const;
  bool hasAnyFilterActive() const;
  void updateRemoveFiltersButtonVisibility() const;
  void updateFilterIndicator() const;
  void rebuildTagsTableOnce();
  void showEmptyTagsTable();
  QVector<TagsTableModel::Row> visibleTagRowsSortedByMark() const;
  TagsTableModel::Row tagsTableRowFor(const TagSession::GameTag& tag, int tagSessionIndex) const;
  void fitTagsTableColumns();
  void restoreTagsTableSelection(quint64 previouslySelectedTagId, int previousVerticalScroll);
  QSet<TagEvent::MainEventKey> refreshFilterMenuActions(bool deferStructureChanges);
  void hideRetiredFilterMenuActions(const QSet<TagEvent::MainEventKey>& liveMainEvents);

  TagSession* tagSession_ = nullptr;
  NotesController* notesController_ = nullptr;
  QPointer<VideoPlayer> videoPlayer_;
  QPointer<QTableView> tagsTable_;
  QPointer<TagsTableModel> tagsModel_;
  QPointer<QMenu> tagsFilterMenu_;
  QPointer<QAction> filterSelectAllAction_;
  QPointer<QAction> filterSelectNoneAction_;
  QPointer<QLabel> tagsFilterIndicator_;
  QPointer<QToolButton> tagsRemoveFiltersButton_;
  QHash<TagEvent::MainEventKey, QPointer<QAction>> filterActionByMainEvent_;
  TagFilter tagFilter_;
  bool rebuildingTagsTable_ = false;
  bool tagsTableRebuildRequested_ = false;
  bool filterMenuStructureSyncDeferred_ = false;
  bool filterMenuForceStructureSync_ = false;
};
