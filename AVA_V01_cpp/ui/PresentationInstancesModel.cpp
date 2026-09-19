#include "PresentationInstancesModel.h"

#include "../style/ThemeColors.h"

#include <QBrush>
#include <QColor>

namespace {

constexpr int kTimeColumn = 0;
constexpr int kTeamColumn = 1;
constexpr int kEventColumn = 2;

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

} // namespace

PresentationInstancesBulkUpdateGuard::PresentationInstancesBulkUpdateGuard(
    PresentationInstancesModel& model)
    : model_(model), previousBulkUpdating_(model.bulkUpdating_) {
  model_.bulkUpdating_ = true;
}

PresentationInstancesBulkUpdateGuard::~PresentationInstancesBulkUpdateGuard() {
  model_.bulkUpdating_ = previousBulkUpdating_;
}

PresentationInstancesModel::PresentationInstancesModel(QObject* parent)
    : QAbstractTableModel(parent) {}

int PresentationInstancesModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) return 0;
  return rows_.size();
}

int PresentationInstancesModel::columnCount(const QModelIndex& parent) const {
  if (parent.isValid()) return 0;
  return 3;
}

QVariant PresentationInstancesModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size()) return {};

  const Row& row = rows_.at(index.row());
  const bool isCurrentClip = currentTagId_ != 0 && row.tagId == currentTagId_;

  if (role == Qt::DisplayRole) {
    switch (index.column()) {
    case kTimeColumn:
      return formatTimestampMs(row.markMs);
    case kTeamColumn:
      return row.teamDisplay;
    case kEventColumn:
      return row.eventLine;
    default:
      return {};
    }
  }

  if (role == Qt::CheckStateRole && index.column() == kTimeColumn) {
    return selectedTagIds_.contains(row.tagId) ? Qt::Checked : Qt::Unchecked;
  }

  if (role == Qt::TextAlignmentRole) {
    return QVariant::fromValue(Qt::AlignLeft | Qt::AlignVCenter);
  }

  if (role == Qt::BackgroundRole && isCurrentClip) {
    return QBrush(Style::ThemeColors::playheadHighlight());
  }

  return {};
}

QVariant PresentationInstancesModel::headerData(int section, Qt::Orientation orientation,
                                                int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
  if (section < 0 || section >= columnHeaders_.size()) return {};
  return columnHeaders_.at(section);
}

Qt::ItemFlags PresentationInstancesModel::flags(const QModelIndex& index) const {
  if (!index.isValid()) return Qt::NoItemFlags;

  Qt::ItemFlags itemFlags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
  if (index.column() == kTimeColumn) {
    itemFlags |= Qt::ItemIsUserCheckable;
  }
  return itemFlags;
}

bool PresentationInstancesModel::setData(const QModelIndex& index, const QVariant& value,
                                         int role) {
  if (bulkUpdating_ || !index.isValid() || index.column() != kTimeColumn || role != Qt::CheckStateRole) {
    return false;
  }

  const Row& row = rows_.at(index.row());
  if (row.tagId == 0) return false;

  const bool checked = value.toInt() == Qt::Checked;
  if (checked == selectedTagIds_.contains(row.tagId)) return true;

  if (checked) {
    selectedTagIds_.insert(row.tagId);
  } else {
    selectedTagIds_.remove(row.tagId);
  }

  emit dataChanged(index, index, {Qt::CheckStateRole});
  emit checkStateChanged(row.tagId, checked);
  return true;
}

void PresentationInstancesModel::setColumnHeaders(const QStringList& headers) {
  columnHeaders_ = headers;
}

void PresentationInstancesModel::setRows(const QVector<Row>& rows) {
  PresentationInstancesBulkUpdateGuard guard(*this);
  beginResetModel();
  rows_ = rows;
  endResetModel();
}

void PresentationInstancesModel::setSelectedTagIds(const QSet<quint64>& selectedTagIds) {
  if (selectedTagIds_ == selectedTagIds) return;

  PresentationInstancesBulkUpdateGuard guard(*this);
  selectedTagIds_ = selectedTagIds;

  if (rows_.isEmpty()) return;
  emit dataChanged(index(0, kTimeColumn), index(rows_.size() - 1, kTimeColumn),
                   {Qt::CheckStateRole});
}

void PresentationInstancesModel::setCurrentTagId(quint64 tagId) {
  if (currentTagId_ == tagId) return;

  const int previousRow = rowForTagId(currentTagId_);
  const int nextRow = rowForTagId(tagId);
  currentTagId_ = tagId;

  if (previousRow >= 0) emitRowDataChanged(previousRow, {Qt::BackgroundRole});
  if (nextRow >= 0 && nextRow != previousRow) emitRowDataChanged(nextRow, {Qt::BackgroundRole});
}

int PresentationInstancesModel::tagSessionIndexAt(int row) const {
  if (row < 0 || row >= rows_.size()) return -1;
  return rows_.at(row).tagSessionIndex;
}

quint64 PresentationInstancesModel::tagIdAt(int row) const {
  if (row < 0 || row >= rows_.size()) return 0;
  return rows_.at(row).tagId;
}

int PresentationInstancesModel::rowForTagId(quint64 tagId) const {
  if (tagId == 0) return -1;
  for (int row = 0; row < rows_.size(); ++row) {
    if (rows_.at(row).tagId == tagId) return row;
  }
  return -1;
}

int PresentationInstancesModel::visibleSelectedCount(const QSet<quint64>& selectedTagIds) const {
  int count = 0;
  for (const Row& row : rows_) {
    if (selectedTagIds.contains(row.tagId)) ++count;
  }
  return count;
}

void PresentationInstancesModel::emitRowDataChanged(int row, const QVector<int>& roles) {
  if (row < 0 || row >= rows_.size()) return;
  emit dataChanged(index(row, 0), index(row, columnCount() - 1), roles);
}
