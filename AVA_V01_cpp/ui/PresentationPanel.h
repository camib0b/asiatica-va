#pragma once

#include <QSet>
#include <QVector>
#include <QWidget>
#include <QtGlobal>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;
class QToolButton;

class TagSession;

/// Side panel of presentation mode: filters the session's tagged events, lets the user select the
/// instances to present, and edits the lead/lag times of the clip currently on screen.
///
/// The panel owns no playback state; it reports the selected instances (as TagSession indexes) and
/// the requested lead/lag edits, and WorkWindow feeds them into the PresentationQueue.
class PresentationPanel final : public QWidget {
  Q_OBJECT

public:
  explicit PresentationPanel(QWidget* parent = nullptr);

  void setTagSession(TagSession* session);
  /// Rebuilds filters and rows from the session, keeping selected instances that still exist.
  void refreshFromSession();

  QVector<int> selectedTagIndexes() const;

  /// Mirrors the queue state into the panel: highlights the row and loads its lead/lag values.
  void setCurrentClip(int tagSessionIndex, qint64 leadMs, qint64 lagMs);
  void clearCurrentClip();

  bool showNotesEnabled() const;
  void setExportEnabled(bool enabled);

  void applyUiStrings();

signals:
  void selectedTagIndexesChanged(const QVector<int>& tagSessionIndexes);
  /// A row was double-clicked: jump to that instance and start presenting it.
  void clipActivated(int tagSessionIndex);
  void currentClipLeadLagEdited(qint64 leadMs, qint64 lagMs);
  void applyLeadLagToAllRequested(qint64 leadMs, qint64 lagMs);
  void showNotesToggled(bool enabled);
  void exportRequested();

private slots:
  void onFilterChanged();
  void onTableItemChanged(QTableWidgetItem* item);
  void onTableCellDoubleClicked(int row, int column);
  void onSelectAllClicked();
  void onSelectNoneClicked();
  void onLeadLagSpinChanged();
  void onApplyLeadLagToAllClicked();

private:
  void buildUi();
  void rebuildEventFilterOptions();
  void rebuildRows();
  void updateSelectionSummary();
  void updateCurrentClipControlsEnabled();
  void emitSelectionChanged();
  void pruneSelectionToExistingTags();
  quint64 tagIdAt(int tagSessionIndex) const;
  bool passesFilters(const QString& mainEvent, const QString& team) const;
  QString teamDisplayName(const QString& teamKey) const;

  TagSession* tagSession_ = nullptr;

  QLabel* titleLabel_ = nullptr;
  QLabel* eventFilterLabel_ = nullptr;
  QComboBox* eventFilterCombo_ = nullptr;
  QLabel* teamFilterLabel_ = nullptr;
  QComboBox* teamFilterCombo_ = nullptr;
  QToolButton* selectAllButton_ = nullptr;
  QToolButton* selectNoneButton_ = nullptr;
  QLabel* selectionSummaryLabel_ = nullptr;
  QTableWidget* instancesTable_ = nullptr;

  QLabel* currentClipTitleLabel_ = nullptr;
  QLabel* leadLabel_ = nullptr;
  QDoubleSpinBox* leadSpinBox_ = nullptr;
  QLabel* lagLabel_ = nullptr;
  QDoubleSpinBox* lagSpinBox_ = nullptr;
  QPushButton* applyToAllButton_ = nullptr;
  QCheckBox* showNotesCheckBox_ = nullptr;
  QPushButton* exportButton_ = nullptr;
  QLabel* keyboardHintLabel_ = nullptr;

  /// Stable GameTag::id values; converted to session indexes at the public API boundary.
  QSet<quint64> selectedTagIdSet_{};
  quint64 currentTagId_ = 0;
  int currentTagSessionIndex_ = -1;
  QVector<int> lastEmittedSelectedIndexes_{};
  bool populatingRows_ = false;
};
