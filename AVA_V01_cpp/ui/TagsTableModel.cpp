#include "TagsTableModel.h"

#include "../style/ThemeColors.h"

#include <QBrush>
#include <algorithm>

namespace {

constexpr int kTimeColumn = 0;
constexpr int kTeamColumn = 1;
constexpr int kEventColumn = 2;

} // namespace

TagsTableModel::TagsTableModel(QObject* parent) : QAbstractTableModel(parent) {}

int TagsTableModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) return 0;
  return rows_.size();
}

int TagsTableModel::columnCount(const QModelIndex& parent) const {
  if (parent.isValid()) return 0;
  return 3;
}

bool TagsTableModel::isPlayheadHighlightedRow(int row) const {
  return playheadHighlightedRows_.contains(row);
}

bool TagsTableModel::isFlashHighlightedRow(int row) const {
  if (flashTagId_ == 0 || row < 0 || row >= rows_.size()) return false;
  return rows_.at(row).tagId == flashTagId_;
}

void TagsTableModel::emitHighlightDataChanged(int row) {
  if (row < 0 || row >= rows_.size()) return;
  emit dataChanged(index(row, kTimeColumn), index(row, kEventColumn), {Qt::BackgroundRole});
}

QSet<int> TagsTableModel::playheadHighlightedRowsFor(qint64 playheadMs) const {
  QSet<int> highlightedRows;
  if (rows_.isEmpty() || playheadMs < 0) return highlightedRows;

  const auto rowLessThanMark = [](const Row& left, qint64 markMs) { return left.markMs < markMs; };
  const auto markLessThanRow = [](qint64 markMs, const Row& right) { return markMs < right.markMs; };

  const auto beginIterator =
      std::lower_bound(rows_.begin(), rows_.end(), playheadMs - kPlayheadNearToleranceMs, rowLessThanMark);
  const auto endIterator =
      std::upper_bound(rows_.begin(), rows_.end(), playheadMs + kPlayheadNearToleranceMs, markLessThanRow);

  for (auto iterator = beginIterator; iterator != endIterator; ++iterator) {
    const qint64 difference =
        (iterator->markMs > playheadMs) ? (iterator->markMs - playheadMs) : (playheadMs - iterator->markMs);
    if (difference <= kPlayheadNearToleranceMs) {
      highlightedRows.insert(static_cast<int>(iterator - rows_.begin()));
    }
  }
  return highlightedRows;
}

QVariant TagsTableModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size()) return {};

  const Row& row = rows_.at(index.row());
  const int column = index.column();

  if (role == Qt::DisplayRole) {
    switch (column) {
    case kTimeColumn:
      return row.timeText;
    case kTeamColumn:
      return row.teamText;
    case kEventColumn:
      return row.eventText;
    default:
      return {};
    }
  }

  if (role == Qt::TextAlignmentRole) {
    return QVariant::fromValue(Qt::AlignLeft | Qt::AlignVCenter);
  }

  if (role == Qt::BackgroundRole) {
    if (column == kTeamColumn && row.teamBackground.isValid()) {
      return QBrush(row.teamBackground);
    }
    if (column == kTimeColumn || column == kEventColumn) {
      if (isFlashHighlightedRow(index.row()) || isPlayheadHighlightedRow(index.row())) {
        return QBrush(Style::ThemeColors::playheadHighlight());
      }
    }
    return {};
  }

  if (role == Qt::ForegroundRole && column == kTeamColumn && row.teamBackground.isValid()) {
    return QBrush(QColor(9, 9, 11));
  }

  return {};
}

QVariant TagsTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
  if (section < 0 || section >= columnHeaders_.size()) return {};
  return columnHeaders_.at(section);
}

void TagsTableModel::setColumnHeaders(const QStringList& headers) {
  columnHeaders_ = headers;
}

void TagsTableModel::setRows(const QVector<Row>& rows) {
  beginResetModel();
  rows_ = rows;
  playheadHighlightedRows_.clear();
  endResetModel();
}

void TagsTableModel::setPlayheadMs(qint64 playheadMs) {
  const QSet<int> nextHighlightedRows = playheadHighlightedRowsFor(playheadMs);
  if (playheadMs_ == playheadMs && nextHighlightedRows == playheadHighlightedRows_) {
    return;
  }

  QSet<int> rowsToRefresh;
  for (int row : playheadHighlightedRows_) {
    if (!nextHighlightedRows.contains(row)) rowsToRefresh.insert(row);
  }
  for (int row : nextHighlightedRows) {
    if (!playheadHighlightedRows_.contains(row)) rowsToRefresh.insert(row);
  }

  playheadMs_ = playheadMs;
  playheadHighlightedRows_ = nextHighlightedRows;

  for (int row : rowsToRefresh) {
    emitHighlightDataChanged(row);
  }
}

void TagsTableModel::setFlashTagId(quint64 tagId) {
  if (flashTagId_ == tagId) return;

  const int previousRow = rowForTagId(flashTagId_);
  const int nextRow = rowForTagId(tagId);
  flashTagId_ = tagId;

  if (previousRow >= 0) emitHighlightDataChanged(previousRow);
  if (nextRow >= 0 && nextRow != previousRow) emitHighlightDataChanged(nextRow);
}

quint64 TagsTableModel::tagIdAt(int row) const {
  if (row < 0 || row >= rows_.size()) return 0;
  return rows_.at(row).tagId;
}

int TagsTableModel::tagSessionIndexAt(int row) const {
  if (row < 0 || row >= rows_.size()) return -1;
  return rows_.at(row).tagSessionIndex;
}

qint64 TagsTableModel::markMsAt(int row) const {
  if (row < 0 || row >= rows_.size()) return 0;
  return rows_.at(row).markMs;
}

int TagsTableModel::rowForTagId(quint64 tagId) const {
  if (tagId == 0) return -1;
  for (int row = 0; row < rows_.size(); ++row) {
    if (rows_.at(row).tagId == tagId) return row;
  }
  return -1;
}
