#include "TagsTableController.h"

#include "../components/VideoPlayer.h"
#include "../i18n/AppLocale.h"
#include "../state/TagEvent.h"
#include "../state/TagSession.h"
#include "NotesController.h"
#include "TagRowDisplay.h"
#include "TagsTableModel.h"

#include <QAbstractItemView>
#include <QAction>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMenu>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStringList>
#include <QTableView>
#include <QVariant>
#include <QTimer>
#include <QToolButton>

#include <algorithm>

namespace {

/// Sets a reentrancy flag for this scope and restores the value it had on entry.
class ScopedReentrancyFlag {
public:
  explicit ScopedReentrancyFlag(bool& flag) : flag_(flag), previousValue_(flag) { flag_ = true; }
  ~ScopedReentrancyFlag() { flag_ = previousValue_; }

  ScopedReentrancyFlag(const ScopedReentrancyFlag&) = delete;
  ScopedReentrancyFlag& operator=(const ScopedReentrancyFlag&) = delete;

private:
  bool& flag_;
  bool previousValue_;
};

}  // namespace

TagsTableController::TagsTableController(QObject* parent)
    : QObject(parent),
      tagSession_(nullptr),
      notesController_(nullptr),
      videoPlayer_(nullptr),
      tagsTable_(nullptr),
      tagsModel_(nullptr),
      tagsFilterMenu_(nullptr),
      filterSelectAllAction_(nullptr),
      filterSelectNoneAction_(nullptr),
      tagsFilterIndicator_(nullptr),
      tagsRemoveFiltersButton_(nullptr),
      filterActionByMainEvent_(),
      tagFilter_(),
      rebuildingTagsTable_(false),
      tagsTableRebuildRequested_(false),
      filterMenuStructureSyncDeferred_(false),
      filterMenuForceStructureSync_(false) {}

TagsTableController::~TagsTableController() = default;

void TagsTableController::setTagSession(TagSession* tagSession) {
  tagSession_ = tagSession;
}

void TagsTableController::setNotesController(NotesController* notesController) {
  notesController_ = notesController;
}

void TagsTableController::setVideoPlayer(VideoPlayer* videoPlayer) {
  videoPlayer_ = videoPlayer;
}

void TagsTableController::setTagsTable(QTableView* tagsTable, TagsTableModel* tagsModel) {
  tagsTable_ = tagsTable;
  tagsModel_ = tagsModel;
}

void TagsTableController::setFilterWidgets(QMenu* filterMenu,
                                           QAction* selectAllAction,
                                           QAction* selectNoneAction,
                                           QLabel* filterIndicator,
                                           QToolButton* removeFiltersButton) {
  tagsFilterMenu_ = filterMenu;
  filterSelectAllAction_ = selectAllAction;
  filterSelectNoneAction_ = selectNoneAction;
  tagsFilterIndicator_ = filterIndicator;
  tagsRemoveFiltersButton_ = removeFiltersButton;
}

void TagsTableController::wireFilterSignals() {
  if (filterSelectAllAction_) {
    connect(filterSelectAllAction_, &QAction::triggered, this, &TagsTableController::onSelectAllFilters);
  }
  if (filterSelectNoneAction_) {
    connect(filterSelectNoneAction_, &QAction::triggered, this, &TagsTableController::onSelectNoneFilters);
  }
  if (tagsFilterMenu_) {
    connect(tagsFilterMenu_, &QMenu::aboutToHide, this, &TagsTableController::onTagsFilterMenuAboutToHide);
  }
  if (tagsRemoveFiltersButton_) {
    connect(tagsRemoveFiltersButton_, &QToolButton::clicked, this, &TagsTableController::onRemoveFilters);
  }
}

void TagsTableController::syncFilterMenu() {
  if (!tagsFilterMenu_ || !filterSelectAllAction_ || !filterSelectNoneAction_) return;

  const bool deferStructureChanges =
      tagsFilterMenu_->isVisible() && !filterMenuForceStructureSync_;
  if (deferStructureChanges) {
    filterMenuStructureSyncDeferred_ = true;
  }

  filterSelectAllAction_->setText(AppLocale::trUi("filter.select_all"));
  filterSelectNoneAction_->setText(AppLocale::trUi("filter.select_none"));

  const QSet<TagEvent::MainEventKey> liveMainEvents =
      refreshFilterMenuActions(deferStructureChanges);
  if (deferStructureChanges) return;

  hideRetiredFilterMenuActions(liveMainEvents);
  filterMenuStructureSyncDeferred_ = false;
}

void TagsTableController::refreshTagsTableRows() {
  if (!tagsTable_ || !tagsModel_) return;
  if (rebuildingTagsTable_) {
    tagsTableRebuildRequested_ = true;
    return;
  }

  const ScopedReentrancyFlag rebuilding(rebuildingTagsTable_);
  do {
    tagsTableRebuildRequested_ = false;
    rebuildTagsTableOnce();
  } while (tagsTableRebuildRequested_);
}

void TagsTableController::updatePlayheadHighlight(qint64 positionMs) {
  if (tagsModel_) tagsModel_->setPlayheadMs(positionMs);
}

void TagsTableController::onFilterByEventPathRequested(const TagEvent::MainEventKey& mainEvent,
                                                       const TagEvent::FollowUpChain& followUps) {
  if (mainEvent.isEmpty()) tagFilter_.clearEventPath();
  else tagFilter_.eventPath = EventPathFilter{mainEvent, followUps};
  syncFilterMenu();
  refreshTagsTableRows();
}

void TagsTableController::onSelectAllFilters() {
  tagFilter_.showAllMainEvents();
  syncFilterMenu();
  refreshTagsTableRows();
}

void TagsTableController::onSelectNoneFilters() {
  QList<TagEvent::MainEventKey> mainEvents;
  if (tagSession_) mainEvents = tagSession_->mainEventCounts().keys();
  tagFilter_.hideAllMainEvents(mainEvents);
  syncFilterMenu();
  refreshTagsTableRows();
}

void TagsTableController::onFilterActionToggled(bool checked) {
  auto* action = qobject_cast<QAction*>(sender());
  if (!action) return;
  const TagEvent::MainEventKey mainEvent = action->data().value<TagEvent::MainEventKey>();
  if (mainEvent.isEmpty()) return;
  if (checked) tagFilter_.hiddenMainEvents.remove(mainEvent);
  else tagFilter_.hiddenMainEvents.insert(mainEvent);
  refreshTagsTableRows();
}

void TagsTableController::onRemoveFilters() {
  tagFilter_.clearEventPath();
  tagFilter_.showAllMainEvents();
  syncFilterMenu();
  refreshTagsTableRows();
}

void TagsTableController::onTagsFilterMenuAboutToHide() {
  if (!filterMenuStructureSyncDeferred_) return;
  QTimer::singleShot(0, this, [this]() { applyDeferredFilterMenuStructureSync(); });
}

void TagsTableController::applyDeferredFilterMenuStructureSync() {
  if (!filterMenuStructureSyncDeferred_) return;
  filterMenuForceStructureSync_ = true;
  syncFilterMenu();
  filterMenuForceStructureSync_ = false;
}

bool TagsTableController::tagPassesEventFilter(const TagEvent::MainEventKey& mainEvent,
                                              const TagEvent::FollowUpChain& followUps) const {
  return tagFilter_.allows(mainEvent, followUps);
}

bool TagsTableController::hasAnyFilterActive() const {
  return tagFilter_.isActive();
}

void TagsTableController::updateRemoveFiltersButtonVisibility() const {
  if (!tagsRemoveFiltersButton_) return;
  if (hasAnyFilterActive()) {
    tagsRemoveFiltersButton_->show();
  } else {
    tagsRemoveFiltersButton_->hide();
  }
}

void TagsTableController::updateFilterIndicator() const {
  if (!tagsFilterIndicator_) return;

  if (tagFilter_.eventPath.has_value() && !tagFilter_.eventPath->mainEvent.isEmpty()) {
    const QString pathText = TagEvent::displayTagLine(tagFilter_.eventPath->mainEvent,
                                                      tagFilter_.eventPath->followUps);
    tagsFilterIndicator_->setText(AppLocale::trUi("filter.indicator") + pathText);
    tagsFilterIndicator_->show();
    return;
  }

  QList<TagEvent::MainEventKey> visibleMainEvents;
  if (tagSession_) {
    visibleMainEvents = TagEvent::sortedByCanonicalName(tagSession_->mainEventCounts().keys());
  }
  QStringList shownLabels;
  for (const TagEvent::MainEventKey& mainEvent : visibleMainEvents) {
    if (tagFilter_.hiddenMainEvents.contains(mainEvent)) continue;
    shownLabels.append(TagEvent::displayLabel(mainEvent));
  }

  if (shownLabels.isEmpty() || shownLabels.size() == visibleMainEvents.size()) {
    tagsFilterIndicator_->hide();
    return;
  }

  shownLabels.sort(Qt::CaseInsensitive);
  tagsFilterIndicator_->setText(AppLocale::trUi("filter.indicator") + shownLabels.join(QStringLiteral(", ")));
  tagsFilterIndicator_->show();
}

void TagsTableController::rebuildTagsTableOnce() {
  const quint64 previouslySelectedTagId = notesController_->selectedTagId();
  const int previousVerticalScroll =
      tagsTable_->verticalScrollBar() ? tagsTable_->verticalScrollBar()->value() : 0;
  // Keep both blockers alive through the note reload. A selection change
  // during the rebuild must not re-enter through the table's currentChanged.
  const QSignalBlocker tableBlocker(tagsTable_);
  const QSignalBlocker selectionBlocker(tagsTable_->selectionModel());

  if (!tagSession_) {
    showEmptyTagsTable();
  } else {
    tagsModel_->setRows(visibleTagRowsSortedByMark());
    fitTagsTableColumns();
    restoreTagsTableSelection(previouslySelectedTagId, previousVerticalScroll);
    if (videoPlayer_) {
      tagsModel_->setPlayheadMs(videoPlayer_->currentPositionMs());
    }
    updateFilterIndicator();
    updateRemoveFiltersButtonVisibility();
    notesController_->refreshMatchNoteMentionCandidates();
  }

  if (notesController_->selectedTagId() != previouslySelectedTagId) {
    notesController_->flushPendingClipNote();
    notesController_->loadClipNoteForSelectedTag();
  }
}

void TagsTableController::showEmptyTagsTable() {
  tagsModel_->setRows({});
  notesController_->refreshMatchNoteMentionCandidates();
  updateFilterIndicator();
  updateRemoveFiltersButtonVisibility();
}

QVector<TagsTableModel::Row> TagsTableController::visibleTagRowsSortedByMark() const {
  struct VisibleTag {
    TagSession::GameTag tag;
    int tagSessionIndex = 0;
  };
  QVector<VisibleTag> visibleTags;
  int tagSessionIndex = 0;
  for (const TagSession::GameTag& tag : tagSession_->tags()) {
    if (tagPassesEventFilter(tag.mainEventKey(), tag.followUpChain())) {
      visibleTags.append({tag, tagSessionIndex});
    }
    tagSessionIndex++;
  }

  std::sort(visibleTags.begin(), visibleTags.end(), [](const VisibleTag& left, const VisibleTag& right) {
    return left.tag.markMs < right.tag.markMs;
  });

  QVector<TagsTableModel::Row> rows;
  rows.reserve(visibleTags.size());
  for (const VisibleTag& visibleTag : visibleTags) {
    rows.append(tagsTableRowFor(visibleTag.tag, visibleTag.tagSessionIndex));
  }
  return rows;
}

TagsTableModel::Row TagsTableController::tagsTableRowFor(const TagSession::GameTag& tag,
                                                         int tagSessionIndex) const {
  TagsTableModel::Row row;
  row.tagId = tag.id;
  row.tagSessionIndex = tagSessionIndex;
  row.markMs = tag.markMs;
  row.teamKey = tag.team;
  row.timeText = TagRowDisplay::formatTimestampMs(tag.markMs);
  row.teamText = TagRowDisplay::displayTeamForTag(tag, tagSession_);
  row.eventText = TagRowDisplay::eventLineForTag(tag);
  row.teamBackground = TagRowDisplay::teamBackgroundForTag(tag, tagSession_);
  return row;
}

void TagsTableController::fitTagsTableColumns() {
  tagsTable_->resizeColumnToContents(0);
  tagsTable_->resizeColumnToContents(1);
}

void TagsTableController::restoreTagsTableSelection(quint64 previouslySelectedTagId,
                                                    int previousVerticalScroll) {
  const int restoredRow = tagsModel_->rowForTagId(previouslySelectedTagId);
  if (restoredRow >= 0) {
    tagsTable_->selectRow(restoredRow);
    tagsTable_->scrollTo(tagsModel_->index(restoredRow, 0), QAbstractItemView::EnsureVisible);
    if (tagsTable_->verticalScrollBar()) {
      tagsTable_->verticalScrollBar()->setValue(previousVerticalScroll);
    }
  } else {
    tagsTable_->scrollToBottom();
  }
}

QSet<TagEvent::MainEventKey> TagsTableController::refreshFilterMenuActions(bool deferStructureChanges) {
  QSet<TagEvent::MainEventKey> liveMainEvents;
  if (!tagSession_) return liveMainEvents;

  const QList<TagEvent::MainEventKey> mainEvents =
      TagEvent::sortedByCanonicalName(tagSession_->mainEventCounts().keys());
  for (const TagEvent::MainEventKey& mainEvent : mainEvents) {
    liveMainEvents.insert(mainEvent);
    QAction* action = filterActionByMainEvent_.value(mainEvent, nullptr);
    if (!action) {
      if (deferStructureChanges) continue;
      action = tagsFilterMenu_->addAction(TagEvent::displayLabel(mainEvent));
      action->setCheckable(true);
      action->setData(QVariant::fromValue(mainEvent));
      connect(action, &QAction::toggled, this, &TagsTableController::onFilterActionToggled);
      filterActionByMainEvent_.insert(mainEvent, action);
    } else {
      action->setText(TagEvent::displayLabel(mainEvent));
    }
    const bool checked = !tagFilter_.hiddenMainEvents.contains(mainEvent);
    if (action->isChecked() != checked) {
      const QSignalBlocker blocker(action);
      action->setChecked(checked);
    }
    if (!deferStructureChanges) action->setVisible(true);
  }
  return liveMainEvents;
}

void TagsTableController::hideRetiredFilterMenuActions(
    const QSet<TagEvent::MainEventKey>& liveMainEvents) {
  for (auto iterator = filterActionByMainEvent_.cbegin(); iterator != filterActionByMainEvent_.cend(); ++iterator) {
    if (!liveMainEvents.contains(iterator.key())) iterator.value()->setVisible(false);
  }
}
