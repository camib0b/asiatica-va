#include "PresentationPanel.h"

#include "../i18n/AppLocale.h"
#include "../state/EventDefaults.h"
#include "../state/TagSession.h"
#include "../state/TimeConvert.h"
#include "../style/StyleProps.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr double kLeadLagStepSeconds = 0.5;
constexpr double kMinLeadLagSeconds = EventDefaults::kMinLeadLagMs / 1000.0;
constexpr double kMaxLeadLagSeconds = EventDefaults::kMaxLeadLagMs / 1000.0;
static_assert(EventDefaults::kMinLeadLagMs == static_cast<qint64>(kMinLeadLagSeconds * 1000.0));
static_assert(EventDefaults::kMaxLeadLagMs == static_cast<qint64>(kMaxLeadLagSeconds * 1000.0));

qint64 leadLagMillisecondsFromSpin(const QDoubleSpinBox* spinBox) {
  return TimeConvert::clampedMillisecondsFromSeconds(
      spinBox->value(), EventDefaults::kMinLeadLagMs, EventDefaults::kMaxLeadLagMs);
}

QDoubleSpinBox* makeLeadLagSpinBox(QWidget* parent) {
  auto* spinBox = new QDoubleSpinBox(parent);
  spinBox->setRange(kMinLeadLagSeconds, kMaxLeadLagSeconds);
  spinBox->setSingleStep(kLeadLagStepSeconds);
  spinBox->setDecimals(1);
  spinBox->setSuffix(QStringLiteral(" s"));
  spinBox->setAlignment(Qt::AlignRight);
  spinBox->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
  return spinBox;
}

} // namespace

PresentationPanel::PresentationPanel(QWidget* parent)
    : QWidget(parent) {
  setObjectName(QStringLiteral("PresentationPanel"));
  buildUi();
  applyUiStrings();
  updateCurrentClipControlsEnabled();
}

PresentationPanel::~PresentationPanel() {
  if (tagSession_) disconnect(tagSession_, nullptr, this, nullptr);
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

void PresentationPanel::buildUi() {
  auto* rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(0, 0, 0, 0);
  rootLayout->setSpacing(8);

  titleLabel_ = new QLabel(this);
  Style::setRole(titleLabel_, "h3");
  rootLayout->addWidget(titleLabel_);

  auto* filterRow = new QHBoxLayout();
  filterRow->setContentsMargins(0, 0, 0, 0);
  filterRow->setSpacing(8);

  eventFilterLabel_ = new QLabel(this);
  Style::setRole(eventFilterLabel_, "muted");
  eventFilterCombo_ = new QComboBox(this);
  eventFilterCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  connect(eventFilterCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &PresentationPanel::onFilterChanged);

  teamFilterLabel_ = new QLabel(this);
  Style::setRole(teamFilterLabel_, "muted");
  teamFilterCombo_ = new QComboBox(this);
  teamFilterCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  connect(teamFilterCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &PresentationPanel::onFilterChanged);

  filterRow->addWidget(eventFilterLabel_);
  filterRow->addWidget(eventFilterCombo_, 1);
  filterRow->addWidget(teamFilterLabel_);
  filterRow->addWidget(teamFilterCombo_, 1);
  rootLayout->addLayout(filterRow);

  auto* selectionRow = new QHBoxLayout();
  selectionRow->setContentsMargins(0, 0, 0, 0);
  selectionRow->setSpacing(6);

  selectAllButton_ = new QToolButton(this);
  Style::setVariant(selectAllButton_, "ghost");
  Style::setSize(selectAllButton_, "sm");
  selectAllButton_->setCursor(Qt::PointingHandCursor);
  connect(selectAllButton_, &QToolButton::clicked, this, &PresentationPanel::onSelectAllClicked);

  selectNoneButton_ = new QToolButton(this);
  Style::setVariant(selectNoneButton_, "ghost");
  Style::setSize(selectNoneButton_, "sm");
  selectNoneButton_->setCursor(Qt::PointingHandCursor);
  connect(selectNoneButton_, &QToolButton::clicked, this, &PresentationPanel::onSelectNoneClicked);

  selectionSummaryLabel_ = new QLabel(this);
  Style::setRole(selectionSummaryLabel_, "muted");

  selectionRow->addWidget(selectAllButton_);
  selectionRow->addWidget(selectNoneButton_);
  selectionRow->addStretch(1);
  selectionRow->addWidget(selectionSummaryLabel_);
  rootLayout->addLayout(selectionRow);

  instancesModel_ = new PresentationInstancesModel(this);
  instancesTable_ = new QTableView(this);
  instancesTable_->setObjectName(QStringLiteral("PresentationInstancesTable"));
  instancesTable_->setModel(instancesModel_);
  instancesTable_->verticalHeader()->hide();
  instancesTable_->setShowGrid(false);
  instancesTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
  instancesTable_->setSelectionMode(QAbstractItemView::SingleSelection);
  instancesTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  instancesTable_->setWordWrap(false);
  instancesTable_->horizontalHeader()->setStretchLastSection(true);
  instancesTable_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  instancesTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  instancesTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  instancesTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  instancesTable_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  connect(instancesModel_, &PresentationInstancesModel::checkStateChanged, this,
          &PresentationPanel::onModelCheckStateChanged);
  connect(instancesTable_, &QTableView::doubleClicked, this,
          &PresentationPanel::onTableRowDoubleClicked);
  rootLayout->addWidget(instancesTable_, 1);

  auto* separator = new QFrame(this);
  separator->setFrameShape(QFrame::HLine);
  Style::setRole(separator, "separator");
  rootLayout->addWidget(separator);

  currentClipTitleLabel_ = new QLabel(this);
  Style::setRole(currentClipTitleLabel_, "h3");
  rootLayout->addWidget(currentClipTitleLabel_);

  auto* leadLagRow = new QHBoxLayout();
  leadLagRow->setContentsMargins(0, 0, 0, 0);
  leadLagRow->setSpacing(8);

  leadLabel_ = new QLabel(this);
  Style::setRole(leadLabel_, "muted");
  leadSpinBox_ = makeLeadLagSpinBox(this);
  leadSpinBox_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  connect(leadSpinBox_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
          &PresentationPanel::onLeadLagSpinChanged);

  lagLabel_ = new QLabel(this);
  Style::setRole(lagLabel_, "muted");
  lagSpinBox_ = makeLeadLagSpinBox(this);
  lagSpinBox_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  connect(lagSpinBox_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
          &PresentationPanel::onLeadLagSpinChanged);

  leadLagRow->addWidget(leadLabel_);
  leadLagRow->addWidget(leadSpinBox_, 1);
  leadLagRow->addWidget(lagLabel_);
  leadLagRow->addWidget(lagSpinBox_, 1);
  rootLayout->addLayout(leadLagRow);

  applyToAllButton_ = new QPushButton(this);
  applyToAllButton_->setCursor(Qt::PointingHandCursor);
  Style::setVariant(applyToAllButton_, "outline");
  connect(applyToAllButton_, &QPushButton::clicked, this,
          &PresentationPanel::onApplyLeadLagToAllClicked);
  rootLayout->addWidget(applyToAllButton_);

  showNotesCheckBox_ = new QCheckBox(this);
  showNotesCheckBox_->setCursor(Qt::PointingHandCursor);
  showNotesCheckBox_->setChecked(true);
  connect(showNotesCheckBox_, &QCheckBox::toggled, this, &PresentationPanel::showNotesToggled);
  rootLayout->addWidget(showNotesCheckBox_);

  exportButton_ = new QPushButton(this);
  exportButton_->setCursor(Qt::PointingHandCursor);
  Style::setVariant(exportButton_, "primary");
  connect(exportButton_, &QPushButton::clicked, this, &PresentationPanel::exportRequested);
  rootLayout->addWidget(exportButton_);

  keyboardHintLabel_ = new QLabel(this);
  Style::setRole(keyboardHintLabel_, "faint");
  keyboardHintLabel_->setWordWrap(true);
  rootLayout->addWidget(keyboardHintLabel_);
}

void PresentationPanel::applyUiStrings() {
  titleLabel_->setText(AppLocale::trUi("presentation.panel_title"));
  eventFilterLabel_->setText(AppLocale::trUi("presentation.filter_event"));
  teamFilterLabel_->setText(AppLocale::trUi("presentation.filter_team"));
  selectAllButton_->setText(AppLocale::trUi("presentation.select_all"));
  selectNoneButton_->setText(AppLocale::trUi("presentation.select_none"));
  currentClipTitleLabel_->setText(AppLocale::trUi("presentation.current_clip"));
  leadLabel_->setText(AppLocale::trUi("clip_durations.col_lead"));
  lagLabel_->setText(AppLocale::trUi("clip_durations.col_lag"));
  applyToAllButton_->setText(AppLocale::trUi("presentation.apply_to_all"));
  applyToAllButton_->setToolTip(AppLocale::trUi("presentation.apply_to_all_tooltip"));
  showNotesCheckBox_->setText(AppLocale::trUi("presentation.show_notes"));
  exportButton_->setText(AppLocale::trUi("presentation.export"));
  keyboardHintLabel_->setText(AppLocale::trUi("presentation.keyboard_hint"));
  instancesModel_->setColumnHeaders({AppLocale::trUi("tags.col_time"), AppLocale::trUi("tags.col_team"),
                                     AppLocale::trUi("tags.col_event")});

  refreshFromSession();
}

// ---------------------------------------------------------------------------
// Session data
// ---------------------------------------------------------------------------

void PresentationPanel::setTagSession(TagSession* session) {
  if (tagSession_ == session) return;
  if (tagSession_) disconnect(tagSession_, nullptr, this, nullptr);

  tagSession_ = session;
  selectedTagIdSet_.clear();
  currentTagId_ = 0;
  currentTagSessionIndex_ = -1;

  if (tagSession_) {
    connect(tagSession_, &TagSession::cleared, this, [this]() {
      selectedTagIdSet_.clear();
      currentTagId_ = 0;
      currentTagSessionIndex_ = -1;
      refreshFromSession();
    });
    connect(tagSession_, &TagSession::tagsImported, this, [this]() {
      selectedTagIdSet_.clear();
      currentTagId_ = 0;
      currentTagSessionIndex_ = -1;
      refreshFromSession();
    });
    connect(tagSession_, &TagSession::tagsChanged, this, &PresentationPanel::refreshFromSession);
    connect(tagSession_, &TagSession::tagNoteChanged, this, [this](int) { refreshFromSession(); });
    connect(tagSession_, &TagSession::gameMetadataChanged, this, &PresentationPanel::refreshFromSession);
  }

  refreshFromSession();
}

void PresentationPanel::refreshFromSession() {
  pruneSelectionToExistingTags();
  rebuildEventFilterOptions();
  rebuildRows();
  updateSelectionSummary();
  updateCurrentClipControlsEnabled();

  const QVector<int> currentIndexes = selectedTagIndexes();
  if (currentIndexes != lastEmittedSelectedIndexes_) {
    lastEmittedSelectedIndexes_ = currentIndexes;
    emit selectedTagIndexesChanged(currentIndexes);
  }
}

bool PresentationPanel::eventFilterValueIsValid(const QString& eventFilter) const {
  if (eventFilter.isEmpty()) return true;
  if (EventDefaults::isTimeControlEvent(eventFilter)) return false;
  if (!tagSession_) return false;
  return tagSession_->mainEventCounts().value(eventFilter, 0) > 0;
}

void PresentationPanel::rebuildEventFilterOptions() {
  const QString previousEvent = eventFilterCombo_->currentData().toString();
  const QSignalBlocker eventBlocker(eventFilterCombo_);
  eventFilterCombo_->clear();
  eventFilterCombo_->addItem(AppLocale::trUi("presentation.all_events"), QString());

  if (tagSession_) {
    const auto& counts = tagSession_->mainEventCounts();
    QStringList mainEvents = counts.keys();
    mainEvents.sort(Qt::CaseInsensitive);
    for (const QString& mainEvent : mainEvents) {
      if (EventDefaults::isTimeControlEvent(mainEvent)) continue;
      const int count = counts.value(mainEvent, 0);
      if (count <= 0) continue;
      eventFilterCombo_->addItem(
          QStringLiteral("%1  (%2)").arg(AppLocale::trEvent(mainEvent)).arg(count), mainEvent);
    }
  }

  int restoredIndex = 0;
  if (eventFilterValueIsValid(previousEvent)) {
    const int foundIndex = eventFilterCombo_->findData(previousEvent);
    restoredIndex = foundIndex >= 0 ? foundIndex : 0;
  }
  eventFilterCombo_->setCurrentIndex(restoredIndex);

  const QString previousTeam = teamFilterCombo_->currentData().toString();
  const QSignalBlocker teamBlocker(teamFilterCombo_);
  teamFilterCombo_->clear();
  teamFilterCombo_->addItem(AppLocale::trUi("presentation.team_all"), QString());
  teamFilterCombo_->addItem(teamDisplayName(QStringLiteral("Home")), QStringLiteral("Home"));
  teamFilterCombo_->addItem(teamDisplayName(QStringLiteral("Away")), QStringLiteral("Away"));
  const int restoredTeamIndex = teamFilterCombo_->findData(previousTeam);
  teamFilterCombo_->setCurrentIndex(restoredTeamIndex >= 0 ? restoredTeamIndex : 0);
}

QVector<PresentationInstancesModel::Row> PresentationPanel::buildVisibleRows() const {
  QVector<PresentationInstancesModel::Row> visibleRows;
  if (!tagSession_) return visibleRows;

  const auto& tags = tagSession_->tags();
  visibleRows.reserve(tags.size());
  for (int tagIndex = 0; tagIndex < tags.size(); ++tagIndex) {
    const TagSession::GameTag& tag = tags.at(tagIndex);
    if (EventDefaults::isTimeControlEvent(tag.mainEvent)) continue;
    if (!passesFilters(tag.mainEvent, tag.team)) continue;

    const QString followUpForDisplay = AppLocale::followUpPathWithoutTeamSegments(
        tag.followUpEvent, tagSession_->homeTeamName(), tagSession_->awayTeamName());
    visibleRows.append({tagIndex, tag.id, tag.markMs, teamDisplayName(tag.team),
                        AppLocale::trDisplayTagLine(tag.mainEvent, followUpForDisplay)});
  }

  std::sort(visibleRows.begin(), visibleRows.end(),
            [](const PresentationInstancesModel::Row& first,
               const PresentationInstancesModel::Row& second) {
              if (first.markMs != second.markMs) return first.markMs < second.markMs;
              return first.tagSessionIndex < second.tagSessionIndex;
            });
  return visibleRows;
}

void PresentationPanel::rebuildRows() {
  const int preservedScrollValue = instancesTable_->verticalScrollBar()->value();
  const quint64 previousCurrentTagId = currentTagId_;

  instancesModel_->setRows(buildVisibleRows());
  instancesModel_->setSelectedTagIds(selectedTagIdSet_);
  instancesModel_->setCurrentTagId(previousCurrentTagId);

  instancesTable_->resizeColumnToContents(0);
  instancesTable_->resizeColumnToContents(1);
  instancesTable_->verticalScrollBar()->setValue(preservedScrollValue);

  const int currentRow = instancesModel_->rowForTagId(currentTagId_);
  if (currentRow >= 0) {
    instancesTable_->scrollTo(instancesModel_->index(currentRow, 0),
                              QAbstractItemView::EnsureVisible);
  }
}

void PresentationPanel::syncModelSelection() {
  instancesModel_->setSelectedTagIds(selectedTagIdSet_);
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

QVector<int> PresentationPanel::selectedTagIndexes() const {
  QVector<int> indexes;
  if (!tagSession_) return indexes;

  indexes.reserve(selectedTagIdSet_.size());
  for (quint64 tagId : selectedTagIdSet_) {
    const int tagIndex = tagSession_->indexOfTagId(tagId);
    if (tagIndex >= 0) indexes.append(tagIndex);
  }
  std::sort(indexes.begin(), indexes.end());
  return indexes;
}

void PresentationPanel::emitSelectionChanged() {
  lastEmittedSelectedIndexes_ = selectedTagIndexes();
  updateSelectionSummary();
  emit selectedTagIndexesChanged(lastEmittedSelectedIndexes_);
}

void PresentationPanel::pruneSelectionToExistingTags() {
  if (!tagSession_) {
    selectedTagIdSet_.clear();
    currentTagId_ = 0;
    currentTagSessionIndex_ = -1;
    return;
  }

  QSet<quint64> existingTagIds;
  const auto& tags = tagSession_->tags();
  existingTagIds.reserve(tags.size());
  for (const TagSession::GameTag& tag : tags) {
    if (tag.id != 0) existingTagIds.insert(tag.id);
  }
  selectedTagIdSet_.intersect(existingTagIds);

  if (currentTagId_ != 0) {
    currentTagSessionIndex_ = tagSession_->indexOfTagId(currentTagId_);
    if (currentTagSessionIndex_ < 0) {
      currentTagId_ = 0;
      currentTagSessionIndex_ = -1;
    }
  } else {
    currentTagSessionIndex_ = -1;
  }

  if (currentTagSessionIndex_ >= 0) {
    const TagSession::GameTag& currentTag = tags.at(currentTagSessionIndex_);
    if (EventDefaults::isTimeControlEvent(currentTag.mainEvent)) {
      currentTagId_ = 0;
      currentTagSessionIndex_ = -1;
    }
  }
}

quint64 PresentationPanel::tagIdAt(int tagSessionIndex) const {
  if (!tagSession_ || !tagSession_->isValidTagIndex(tagSessionIndex)) return 0;
  return tagSession_->tags().at(tagSessionIndex).id;
}

bool PresentationPanel::resolveCurrentClip(int tagSessionIndex, int* resolvedIndex,
                                           quint64* resolvedTagId) const {
  if (!resolvedIndex || !resolvedTagId) return false;
  *resolvedIndex = -1;
  *resolvedTagId = 0;

  if (!tagSession_ || !tagSession_->isValidTagIndex(tagSessionIndex)) return false;

  const TagSession::GameTag& tag = tagSession_->tags().at(tagSessionIndex);
  if (tag.id == 0) return false;
  if (EventDefaults::isTimeControlEvent(tag.mainEvent)) return false;

  *resolvedIndex = tagSessionIndex;
  *resolvedTagId = tag.id;
  return true;
}

void PresentationPanel::onFilterChanged() {
  pruneSelectionToExistingTags();
  rebuildRows();
  updateSelectionSummary();
  updateCurrentClipControlsEnabled();
}

void PresentationPanel::onModelCheckStateChanged(quint64 tagId, bool checked) {
  if (tagId == 0) return;
  if (checked) {
    selectedTagIdSet_.insert(tagId);
  } else {
    selectedTagIdSet_.remove(tagId);
  }
  emitSelectionChanged();
}

void PresentationPanel::onTableRowDoubleClicked(const QModelIndex& index) {
  if (!index.isValid() || !tagSession_) return;

  const int tagSessionIndex = instancesModel_->tagSessionIndexAt(index.row());
  const quint64 tagId = instancesModel_->tagIdAt(index.row());
  if (tagId == 0 || !tagSession_->isValidTagIndex(tagSessionIndex)) return;

  if (!selectedTagIdSet_.contains(tagId)) {
    selectedTagIdSet_.insert(tagId);
    syncModelSelection();
    emitSelectionChanged();
  }
  emit clipActivated(tagSessionIndex);
}

void PresentationPanel::onSelectAllClicked() {
  for (int row = 0; row < instancesModel_->rowCount(); ++row) {
    const quint64 tagId = instancesModel_->tagIdAt(row);
    if (tagId != 0) selectedTagIdSet_.insert(tagId);
  }
  syncModelSelection();
  emitSelectionChanged();
}

void PresentationPanel::onSelectNoneClicked() {
  // Only clears the instances currently listed, so filtered-out picks are preserved.
  for (int row = 0; row < instancesModel_->rowCount(); ++row) {
    const quint64 tagId = instancesModel_->tagIdAt(row);
    if (tagId != 0) selectedTagIdSet_.remove(tagId);
  }
  syncModelSelection();
  emitSelectionChanged();
}

void PresentationPanel::updateSelectionSummary() {
  const int visibleSelectedCount = instancesModel_->visibleSelectedCount(selectedTagIdSet_);
  const int listedCount = instancesModel_->rowCount();
  const int totalSelectedCount = selectedTagIndexes().size();
  selectionSummaryLabel_->setText(
      AppLocale::trUi("presentation.selection_summary")
          .arg(visibleSelectedCount)
          .arg(listedCount)
          .arg(totalSelectedCount));
}

// ---------------------------------------------------------------------------
// Current clip
// ---------------------------------------------------------------------------

void PresentationPanel::setCurrentClip(int tagSessionIndex, qint64 leadMs, qint64 lagMs) {
  int resolvedIndex = -1;
  quint64 resolvedTagId = 0;
  resolveCurrentClip(tagSessionIndex, &resolvedIndex, &resolvedTagId);

  const bool currentClipChanged = currentTagSessionIndex_ != resolvedIndex;
  currentTagSessionIndex_ = resolvedIndex;
  currentTagId_ = resolvedTagId;

  {
    const QSignalBlocker leadBlocker(leadSpinBox_);
    const QSignalBlocker lagBlocker(lagSpinBox_);
    leadSpinBox_->setValue(leadMs / 1000.0);
    lagSpinBox_->setValue(lagMs / 1000.0);
  }

  if (currentClipChanged) {
    instancesModel_->setCurrentTagId(resolvedTagId);
    const int currentRow = instancesModel_->rowForTagId(resolvedTagId);
    if (currentRow >= 0) {
      instancesTable_->scrollTo(instancesModel_->index(currentRow, 0),
                                QAbstractItemView::EnsureVisible);
    }
  }
  updateCurrentClipControlsEnabled();
}

void PresentationPanel::clearCurrentClip() {
  currentTagSessionIndex_ = -1;
  currentTagId_ = 0;
  instancesModel_->setCurrentTagId(0);
  updateCurrentClipControlsEnabled();
}

bool PresentationPanel::showNotesEnabled() const { return showNotesCheckBox_->isChecked(); }

void PresentationPanel::setExportEnabled(bool enabled) { exportButton_->setEnabled(enabled); }

void PresentationPanel::updateCurrentClipControlsEnabled() {
  const bool hasCurrentClip =
      tagSession_ && tagSession_->isValidTagIndex(currentTagSessionIndex_) && currentTagId_ != 0;
  leadSpinBox_->setEnabled(hasCurrentClip);
  lagSpinBox_->setEnabled(hasCurrentClip);
  applyToAllButton_->setEnabled(hasCurrentClip);
}

void PresentationPanel::onLeadLagSpinChanged() {
  if (currentTagSessionIndex_ < 0 || !tagSession_) return;
  if (!tagSession_->isValidTagIndex(currentTagSessionIndex_) || currentTagId_ == 0) return;
  emit currentClipLeadLagEdited(leadLagMillisecondsFromSpin(leadSpinBox_),
                                leadLagMillisecondsFromSpin(lagSpinBox_));
}

void PresentationPanel::onApplyLeadLagToAllClicked() {
  emit applyLeadLagToAllRequested(leadLagMillisecondsFromSpin(leadSpinBox_),
                                  leadLagMillisecondsFromSpin(lagSpinBox_));
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

bool PresentationPanel::passesFilters(const QString& mainEvent, const QString& team) const {
  const QString eventFilter = eventFilterCombo_->currentData().toString();
  if (!eventFilter.isEmpty() && mainEvent != eventFilter) return false;

  const QString teamFilter = teamFilterCombo_->currentData().toString();
  if (!teamFilter.isEmpty() && team != teamFilter) return false;

  return true;
}

QString PresentationPanel::teamDisplayName(const QString& teamKey) const {
  if (teamKey == QStringLiteral("Home")) {
    const QString name = tagSession_ ? tagSession_->homeTeamName() : QString();
    return name.isEmpty() ? AppLocale::trUi("export.team_home_default") : name;
  }
  if (teamKey == QStringLiteral("Away")) {
    const QString name = tagSession_ ? tagSession_->awayTeamName() : QString();
    return name.isEmpty() ? AppLocale::trUi("export.team_away_default") : name;
  }
  return teamKey.isEmpty() ? QStringLiteral("—") : teamKey;
}
