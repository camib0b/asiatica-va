#include "PresentationPanel.h"

#include "../i18n/AppLocale.h"
#include "../state/EventDefaults.h"
#include "../state/TagSession.h"
#include "../style/StyleProps.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QBrush>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int kTagIndexRole = Qt::UserRole;
constexpr double kMinLeadLagSeconds = 0.0;
constexpr double kMaxLeadLagSeconds = 60.0;
constexpr double kLeadLagStepSeconds = 0.5;

const QColor kCurrentClipRowColor(147, 197, 253);  // same light blue as the tag-list highlight

QString formatTimestampMs(qint64 positionMs) {
  if (positionMs < 0) positionMs = 0;
  const qint64 totalSeconds = positionMs / 1000;
  const qint64 hours = totalSeconds / 3600;
  const qint64 minutes = (totalSeconds / 60) % 60;
  const qint64 seconds = totalSeconds % 60;

  if (hours > 0) {
    return QStringLiteral("%1:%2:%3")
        .arg(hours)
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
  }
  return QStringLiteral("%1:%2")
      .arg(totalSeconds / 60, 2, 10, QChar('0'))
      .arg(seconds, 2, 10, QChar('0'));
}

QDoubleSpinBox* makeLeadLagSpinBox(QWidget* parent) {
  auto* spinBox = new QDoubleSpinBox(parent);
  spinBox->setRange(kMinLeadLagSeconds, kMaxLeadLagSeconds);
  spinBox->setSingleStep(kLeadLagStepSeconds);
  spinBox->setDecimals(1);
  spinBox->setSuffix(QStringLiteral(" s"));
  spinBox->setMinimumHeight(32);
  spinBox->setAlignment(Qt::AlignRight);
  spinBox->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
  return spinBox;
}

} // namespace

PresentationPanel::PresentationPanel(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("PresentationPanel"));
  buildUi();
  applyUiStrings();
  updateCurrentClipControlsEnabled();
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

  auto* filterGrid = new QGridLayout();
  filterGrid->setContentsMargins(0, 0, 0, 0);
  filterGrid->setHorizontalSpacing(8);
  filterGrid->setVerticalSpacing(6);

  eventFilterLabel_ = new QLabel(this);
  Style::setRole(eventFilterLabel_, "muted");
  eventFilterCombo_ = new QComboBox(this);
  eventFilterCombo_->setMinimumHeight(32);
  connect(eventFilterCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &PresentationPanel::onFilterChanged);

  teamFilterLabel_ = new QLabel(this);
  Style::setRole(teamFilterLabel_, "muted");
  teamFilterCombo_ = new QComboBox(this);
  teamFilterCombo_->setMinimumHeight(32);
  connect(teamFilterCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &PresentationPanel::onFilterChanged);

  filterGrid->addWidget(eventFilterLabel_, 0, 0);
  filterGrid->addWidget(eventFilterCombo_, 0, 1);
  filterGrid->addWidget(teamFilterLabel_, 1, 0);
  filterGrid->addWidget(teamFilterCombo_, 1, 1);
  filterGrid->setColumnStretch(1, 1);
  rootLayout->addLayout(filterGrid);

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

  instancesTable_ = new QTableWidget(this);
  instancesTable_->setObjectName(QStringLiteral("PresentationInstancesTable"));
  instancesTable_->setColumnCount(3);
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
  connect(instancesTable_, &QTableWidget::itemChanged, this,
          &PresentationPanel::onTableItemChanged);
  connect(instancesTable_, &QTableWidget::cellDoubleClicked, this,
          &PresentationPanel::onTableCellDoubleClicked);
  rootLayout->addWidget(instancesTable_, 1);

  auto* separator = new QFrame(this);
  separator->setFrameShape(QFrame::HLine);
  Style::setRole(separator, "separator");
  rootLayout->addWidget(separator);

  currentClipTitleLabel_ = new QLabel(this);
  Style::setRole(currentClipTitleLabel_, "h3");
  rootLayout->addWidget(currentClipTitleLabel_);

  auto* leadLagGrid = new QGridLayout();
  leadLagGrid->setContentsMargins(0, 0, 0, 0);
  leadLagGrid->setHorizontalSpacing(8);
  leadLagGrid->setVerticalSpacing(6);

  leadLabel_ = new QLabel(this);
  Style::setRole(leadLabel_, "muted");
  leadSpinBox_ = makeLeadLagSpinBox(this);
  connect(leadSpinBox_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
          [this](double) { onLeadLagSpinChanged(); });

  lagLabel_ = new QLabel(this);
  Style::setRole(lagLabel_, "muted");
  lagSpinBox_ = makeLeadLagSpinBox(this);
  connect(lagSpinBox_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
          [this](double) { onLeadLagSpinChanged(); });

  leadLagGrid->addWidget(leadLabel_, 0, 0);
  leadLagGrid->addWidget(leadSpinBox_, 0, 1);
  leadLagGrid->addWidget(lagLabel_, 1, 0);
  leadLagGrid->addWidget(lagSpinBox_, 1, 1);
  leadLagGrid->setColumnStretch(1, 1);
  rootLayout->addLayout(leadLagGrid);

  applyToAllButton_ = new QPushButton(this);
  applyToAllButton_->setCursor(Qt::PointingHandCursor);
  Style::setVariant(applyToAllButton_, "outline");
  connect(applyToAllButton_, &QPushButton::clicked, this,
          &PresentationPanel::onApplyLeadLagToAllClicked);
  rootLayout->addWidget(applyToAllButton_);

  showNotesCheckBox_ = new QCheckBox(this);
  showNotesCheckBox_->setCursor(Qt::PointingHandCursor);
  showNotesCheckBox_->setChecked(true);
  connect(showNotesCheckBox_, &QCheckBox::toggled, this,
          [this](bool checked) { emit showNotesToggled(checked); });
  rootLayout->addWidget(showNotesCheckBox_);

  exportButton_ = new QPushButton(this);
  exportButton_->setCursor(Qt::PointingHandCursor);
  Style::setVariant(exportButton_, "primary");
  connect(exportButton_, &QPushButton::clicked, this, [this]() { emit exportRequested(); });
  rootLayout->addWidget(exportButton_);

  keyboardHintLabel_ = new QLabel(this);
  Style::setRole(keyboardHintLabel_, "faint");
  keyboardHintLabel_->setWordWrap(true);
  rootLayout->addWidget(keyboardHintLabel_);
}

void PresentationPanel::applyUiStrings() {
  if (titleLabel_) titleLabel_->setText(AppLocale::trUi("presentation.panel_title"));
  if (eventFilterLabel_) eventFilterLabel_->setText(AppLocale::trUi("presentation.filter_event"));
  if (teamFilterLabel_) teamFilterLabel_->setText(AppLocale::trUi("presentation.filter_team"));
  if (selectAllButton_) selectAllButton_->setText(AppLocale::trUi("presentation.select_all"));
  if (selectNoneButton_) selectNoneButton_->setText(AppLocale::trUi("presentation.select_none"));
  if (currentClipTitleLabel_) {
    currentClipTitleLabel_->setText(AppLocale::trUi("presentation.current_clip"));
  }
  if (leadLabel_) leadLabel_->setText(AppLocale::trUi("clip_durations.col_lead"));
  if (lagLabel_) lagLabel_->setText(AppLocale::trUi("clip_durations.col_lag"));
  if (applyToAllButton_) {
    applyToAllButton_->setText(AppLocale::trUi("presentation.apply_to_all"));
    applyToAllButton_->setToolTip(AppLocale::trUi("presentation.apply_to_all_tooltip"));
  }
  if (showNotesCheckBox_) showNotesCheckBox_->setText(AppLocale::trUi("presentation.show_notes"));
  if (exportButton_) exportButton_->setText(AppLocale::trUi("presentation.export"));
  if (keyboardHintLabel_) keyboardHintLabel_->setText(AppLocale::trUi("presentation.keyboard_hint"));
  if (instancesTable_) {
    instancesTable_->setHorizontalHeaderLabels({AppLocale::trUi("tags.col_time"),
                                                AppLocale::trUi("tags.col_team"),
                                                AppLocale::trUi("tags.col_event")});
  }

  refreshFromSession();
}

// ---------------------------------------------------------------------------
// Session data
// ---------------------------------------------------------------------------

void PresentationPanel::setTagSession(TagSession* session) {
  if (tagSession_ == session) return;
  if (tagSession_) disconnect(tagSession_, nullptr, this, nullptr);

  tagSession_ = session;
  tickedTagIndexes_.clear();
  currentTagSessionIndex_ = -1;

  if (tagSession_) {
    connect(tagSession_, &TagSession::cleared, this, [this]() {
      tickedTagIndexes_.clear();
      currentTagSessionIndex_ = -1;
      refreshFromSession();
      emitSelectionChanged();
    });
    connect(tagSession_, &TagSession::tagsImported, this, [this]() {
      tickedTagIndexes_.clear();
      currentTagSessionIndex_ = -1;
      refreshFromSession();
      emitSelectionChanged();
    });
    connect(tagSession_, &TagSession::statsChanged, this,
            &PresentationPanel::refreshFromSession);
    connect(tagSession_, &TagSession::tagNoteChanged, this,
            [this](int) { refreshFromSession(); });
  }

  refreshFromSession();
}

void PresentationPanel::refreshFromSession() {
  rebuildEventFilterOptions();
  rebuildRows();
  updateSelectionSummary();
  updateCurrentClipControlsEnabled();
}

void PresentationPanel::rebuildEventFilterOptions() {
  if (!eventFilterCombo_) return;

  const QString previousEvent = eventFilterCombo_->currentData().toString();
  const QSignalBlocker eventBlocker(eventFilterCombo_);
  eventFilterCombo_->clear();
  eventFilterCombo_->addItem(AppLocale::trUi("presentation.all_events"), QString());

  if (tagSession_) {
    const auto& counts = tagSession_->mainEventCounts();
    QStringList mainEvents = counts.keys();
    mainEvents.sort(Qt::CaseInsensitive);
    for (const QString& mainEvent : mainEvents) {
      // Time-control codes (game start, quarters, timeouts) mark structure, not presentable plays.
      if (EventDefaults::isTimeControlEvent(mainEvent)) continue;
      const int count = counts.value(mainEvent, 0);
      if (count <= 0) continue;
      eventFilterCombo_->addItem(
          QStringLiteral("%1  (%2)").arg(AppLocale::trEvent(mainEvent)).arg(count), mainEvent);
    }
  }

  const int restoredIndex = eventFilterCombo_->findData(previousEvent);
  eventFilterCombo_->setCurrentIndex(restoredIndex >= 0 ? restoredIndex : 0);

  if (!teamFilterCombo_) return;
  const QString previousTeam = teamFilterCombo_->currentData().toString();
  const QSignalBlocker teamBlocker(teamFilterCombo_);
  teamFilterCombo_->clear();
  teamFilterCombo_->addItem(AppLocale::trUi("export.team_all"), QString());
  teamFilterCombo_->addItem(teamDisplayName(QStringLiteral("Home")), QStringLiteral("Home"));
  teamFilterCombo_->addItem(teamDisplayName(QStringLiteral("Away")), QStringLiteral("Away"));
  const int restoredTeamIndex = teamFilterCombo_->findData(previousTeam);
  teamFilterCombo_->setCurrentIndex(restoredTeamIndex >= 0 ? restoredTeamIndex : 0);
}

void PresentationPanel::rebuildRows() {
  if (!instancesTable_) return;

  const int preservedScrollValue = instancesTable_->verticalScrollBar()->value();
  QTableWidgetItem* currentClipItem = nullptr;

  populatingRows_ = true;
  instancesTable_->setRowCount(0);

  if (tagSession_) {
    struct VisibleTag {
      int tagSessionIndex;
      qint64 markMs;
      QString team;
      QString eventLine;
    };
    QVector<VisibleTag> visibleTags;

    const auto& tags = tagSession_->tags();
    for (int tagIndex = 0; tagIndex < tags.size(); ++tagIndex) {
      const TagSession::GameTag& tag = tags.at(tagIndex);
      if (EventDefaults::isTimeControlEvent(tag.mainEvent)) continue;
      if (!passesFilters(tag.mainEvent, tag.team)) continue;

      const QString followUpForDisplay = AppLocale::followUpPathWithoutTeamSegments(
          tag.followUpEvent, tagSession_->homeTeamName(), tagSession_->awayTeamName());
      visibleTags.append({tagIndex, tag.positionMs, tag.team,
                          AppLocale::trDisplayTagLine(tag.mainEvent, followUpForDisplay)});
    }

    std::sort(visibleTags.begin(), visibleTags.end(),
              [](const VisibleTag& first, const VisibleTag& second) {
                if (first.markMs != second.markMs) return first.markMs < second.markMs;
                return first.tagSessionIndex < second.tagSessionIndex;
              });

    instancesTable_->setRowCount(visibleTags.size());
    for (int row = 0; row < visibleTags.size(); ++row) {
      const VisibleTag& visibleTag = visibleTags.at(row);

      auto* timeItem = new QTableWidgetItem(formatTimestampMs(visibleTag.markMs));
      timeItem->setData(kTagIndexRole, visibleTag.tagSessionIndex);
      timeItem->setFlags((timeItem->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
      timeItem->setCheckState(tickedTagIndexes_.contains(visibleTag.tagSessionIndex)
                                  ? Qt::Checked
                                  : Qt::Unchecked);
      timeItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);

      auto* teamItem = new QTableWidgetItem(teamDisplayName(visibleTag.team));
      teamItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);

      auto* eventItem = new QTableWidgetItem(visibleTag.eventLine);
      eventItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);

      instancesTable_->setItem(row, 0, timeItem);
      instancesTable_->setItem(row, 1, teamItem);
      instancesTable_->setItem(row, 2, eventItem);

      const bool isCurrentClip = visibleTag.tagSessionIndex == currentTagSessionIndex_;
      const QBrush rowBrush = isCurrentClip ? QBrush(kCurrentClipRowColor) : QBrush();
      timeItem->setBackground(rowBrush);
      teamItem->setBackground(rowBrush);
      eventItem->setBackground(rowBrush);
      if (isCurrentClip) currentClipItem = timeItem;
    }
  }

  instancesTable_->resizeColumnToContents(0);
  instancesTable_->resizeColumnToContents(1);
  populatingRows_ = false;

  instancesTable_->verticalScrollBar()->setValue(preservedScrollValue);
  if (currentClipItem) {
    instancesTable_->scrollToItem(currentClipItem, QAbstractItemView::EnsureVisible);
  }
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

QVector<int> PresentationPanel::selectedTagIndexes() const {
  QVector<int> indexes;
  if (!tagSession_) return indexes;

  const auto& tags = tagSession_->tags();
  indexes.reserve(tickedTagIndexes_.size());
  for (int tagIndex = 0; tagIndex < tags.size(); ++tagIndex) {
    if (tickedTagIndexes_.contains(tagIndex)) indexes.append(tagIndex);
  }
  return indexes;
}

void PresentationPanel::emitSelectionChanged() {
  updateSelectionSummary();
  emit selectedTagIndexesChanged(selectedTagIndexes());
}

void PresentationPanel::onFilterChanged() {
  rebuildRows();
  updateSelectionSummary();
}

void PresentationPanel::onTableItemChanged(QTableWidgetItem* item) {
  if (populatingRows_ || !item || item->column() != 0) return;

  const QVariant tagIndexValue = item->data(kTagIndexRole);
  if (!tagIndexValue.isValid()) return;
  const int tagSessionIndex = tagIndexValue.toInt();

  if (item->checkState() == Qt::Checked) {
    tickedTagIndexes_.insert(tagSessionIndex);
  } else {
    tickedTagIndexes_.remove(tagSessionIndex);
  }
  emitSelectionChanged();
}

void PresentationPanel::onTableCellDoubleClicked(int row, int /*column*/) {
  if (!instancesTable_) return;
  QTableWidgetItem* timeItem = instancesTable_->item(row, 0);
  if (!timeItem) return;

  const QVariant tagIndexValue = timeItem->data(kTagIndexRole);
  if (!tagIndexValue.isValid()) return;
  const int tagSessionIndex = tagIndexValue.toInt();

  // Double-clicking an instance also queues it, so the presenter can jump straight to a clip.
  if (!tickedTagIndexes_.contains(tagSessionIndex)) {
    tickedTagIndexes_.insert(tagSessionIndex);
    const QSignalBlocker tableBlocker(instancesTable_);
    timeItem->setCheckState(Qt::Checked);
    emitSelectionChanged();
  }
  emit clipActivated(tagSessionIndex);
}

void PresentationPanel::onSelectAllClicked() {
  if (!instancesTable_) return;
  for (int row = 0; row < instancesTable_->rowCount(); ++row) {
    QTableWidgetItem* timeItem = instancesTable_->item(row, 0);
    if (!timeItem) continue;
    const QVariant tagIndexValue = timeItem->data(kTagIndexRole);
    if (!tagIndexValue.isValid()) continue;
    tickedTagIndexes_.insert(tagIndexValue.toInt());
  }
  rebuildRows();
  emitSelectionChanged();
}

void PresentationPanel::onSelectNoneClicked() {
  if (!instancesTable_) return;
  // Only clears the instances currently listed, so filtered-out picks are preserved.
  for (int row = 0; row < instancesTable_->rowCount(); ++row) {
    QTableWidgetItem* timeItem = instancesTable_->item(row, 0);
    if (!timeItem) continue;
    const QVariant tagIndexValue = timeItem->data(kTagIndexRole);
    if (!tagIndexValue.isValid()) continue;
    tickedTagIndexes_.remove(tagIndexValue.toInt());
  }
  rebuildRows();
  emitSelectionChanged();
}

void PresentationPanel::updateSelectionSummary() {
  if (!selectionSummaryLabel_) return;
  const int listedCount = instancesTable_ ? instancesTable_->rowCount() : 0;
  selectionSummaryLabel_->setText(AppLocale::trUi("presentation.selection_summary")
                                      .arg(tickedTagIndexes_.size())
                                      .arg(listedCount));
}

// ---------------------------------------------------------------------------
// Current clip
// ---------------------------------------------------------------------------

void PresentationPanel::setCurrentClip(int tagSessionIndex, qint64 leadMs, qint64 lagMs) {
  const bool currentClipChanged = currentTagSessionIndex_ != tagSessionIndex;
  currentTagSessionIndex_ = tagSessionIndex;

  if (leadSpinBox_ && lagSpinBox_) {
    const QSignalBlocker leadBlocker(leadSpinBox_);
    const QSignalBlocker lagBlocker(lagSpinBox_);
    leadSpinBox_->setValue(leadMs / 1000.0);
    lagSpinBox_->setValue(lagMs / 1000.0);
  }

  // Only the row highlight depends on the current clip; skip the rebuild when it did not move.
  if (currentClipChanged) rebuildRows();
  updateCurrentClipControlsEnabled();
}

void PresentationPanel::clearCurrentClip() {
  currentTagSessionIndex_ = -1;
  rebuildRows();
  updateCurrentClipControlsEnabled();
}

bool PresentationPanel::showNotesEnabled() const {
  return showNotesCheckBox_ && showNotesCheckBox_->isChecked();
}

void PresentationPanel::setExportEnabled(bool enabled) {
  if (exportButton_) exportButton_->setEnabled(enabled);
}

void PresentationPanel::updateCurrentClipControlsEnabled() {
  const bool hasCurrentClip = currentTagSessionIndex_ >= 0;
  if (leadSpinBox_) leadSpinBox_->setEnabled(hasCurrentClip);
  if (lagSpinBox_) lagSpinBox_->setEnabled(hasCurrentClip);
  if (applyToAllButton_) applyToAllButton_->setEnabled(hasCurrentClip);
}

void PresentationPanel::onLeadLagSpinChanged() {
  if (currentTagSessionIndex_ < 0 || !leadSpinBox_ || !lagSpinBox_) return;
  emit currentClipLeadLagEdited(static_cast<qint64>(leadSpinBox_->value() * 1000.0),
                                static_cast<qint64>(lagSpinBox_->value() * 1000.0));
}

void PresentationPanel::onApplyLeadLagToAllClicked() {
  if (!leadSpinBox_ || !lagSpinBox_) return;
  emit applyLeadLagToAllRequested(static_cast<qint64>(leadSpinBox_->value() * 1000.0),
                                  static_cast<qint64>(lagSpinBox_->value() * 1000.0));
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

bool PresentationPanel::passesFilters(const QString& mainEvent, const QString& team) const {
  const QString eventFilter = eventFilterCombo_ ? eventFilterCombo_->currentData().toString() : QString();
  if (!eventFilter.isEmpty() && mainEvent != eventFilter) return false;

  const QString teamFilter = teamFilterCombo_ ? teamFilterCombo_->currentData().toString() : QString();
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
