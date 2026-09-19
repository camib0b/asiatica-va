#pragma once

#include <QAbstractTableModel>
#include <QSet>
#include <QVector>
#include <QtGlobal>

class TagSession;

/// Table model for presentation-mode tag instances: time, team, event columns with
/// checkable rows and current-clip row highlighting.
class PresentationInstancesModel final : public QAbstractTableModel {
  Q_OBJECT

public:
  struct Row {
    int tagSessionIndex = -1;
    quint64 tagId = 0;
    qint64 markMs = 0;
    QString teamDisplay;
    QString eventLine;
  };

  explicit PresentationInstancesModel(QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  int columnCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
  Qt::ItemFlags flags(const QModelIndex& index) const override;
  bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;

  void setColumnHeaders(const QStringList& headers);

  /// Replaces all rows (e.g. after tagsChanged or filter change). Preserves scroll position externally.
  void setRows(const QVector<Row>& rows);

  /// Updates check-state display without rebuilding row data.
  void setSelectedTagIds(const QSet<quint64>& selectedTagIds);

  /// Updates row highlight without rebuilding row data.
  void setCurrentTagId(quint64 tagId);

  int tagSessionIndexAt(int row) const;
  quint64 tagIdAt(int row) const;
  int rowForTagId(quint64 tagId) const;

  int visibleSelectedCount(const QSet<quint64>& selectedTagIds) const;

signals:
  void checkStateChanged(quint64 tagId, bool checked);

private:
  void emitRowDataChanged(int row, const QVector<int>& roles);

  QStringList columnHeaders_;
  QVector<Row> rows_;
  QSet<quint64> selectedTagIds_;
  quint64 currentTagId_ = 0;
  bool bulkUpdating_ = false;

  friend class PresentationInstancesBulkUpdateGuard;
};

/// RAII guard: suppresses checkStateChanged while programmatically updating the model.
class PresentationInstancesBulkUpdateGuard final {
public:
  explicit PresentationInstancesBulkUpdateGuard(PresentationInstancesModel& model);
  ~PresentationInstancesBulkUpdateGuard();

  PresentationInstancesBulkUpdateGuard(const PresentationInstancesBulkUpdateGuard&) = delete;
  PresentationInstancesBulkUpdateGuard& operator=(const PresentationInstancesBulkUpdateGuard&) = delete;

private:
  PresentationInstancesModel& model_;
  bool previousBulkUpdating_ = false;
};
