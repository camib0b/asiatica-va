#pragma once

#include <QAbstractTableModel>
#include <QColor>
#include <QSet>
#include <QVector>
#include <QtGlobal>

/// Table model for the work-window tags list: time, team, event columns with
/// incremental playhead and new-tag flash highlighting.
class TagsTableModel final : public QAbstractTableModel {
  Q_OBJECT

public:
  struct Row {
    quint64 tagId = 0;
    int tagSessionIndex = -1;
    qint64 markMs = 0;
    QString teamKey;
    QString timeText;
    QString teamText;
    QString eventText;
    QColor teamBackground;
  };

  static constexpr qint64 kPlayheadNearToleranceMs = 2000;

  explicit TagsTableModel(QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  int columnCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

  void setColumnHeaders(const QStringList& headers);
  void setRows(const QVector<Row>& rows);

  void setPlayheadMs(qint64 playheadMs);
  void setFlashTagId(quint64 tagId);

  quint64 tagIdAt(int row) const;
  int tagSessionIndexAt(int row) const;
  qint64 markMsAt(int row) const;
  int rowForTagId(quint64 tagId) const;

private:
  bool isPlayheadHighlightedRow(int row) const;
  bool isFlashHighlightedRow(int row) const;
  void emitHighlightDataChanged(int row);
  QSet<int> playheadHighlightedRowsFor(qint64 playheadMs) const;

  QStringList columnHeaders_;
  QVector<Row> rows_;
  qint64 playheadMs_ = -1;
  quint64 flashTagId_ = 0;
  QSet<int> playheadHighlightedRows_;
};
