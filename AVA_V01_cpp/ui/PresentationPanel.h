#pragma once

#include "PresentationInstancesModel.h"

#include <QPointer>
#include <QSet>
#include <QVector>
#include <QWidget>
#include <QtGlobal>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QEvent;
class QLabel;
class QPushButton;
class QTableView;
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
  ~PresentationPanel() override;

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

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

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
  void onModelCheckStateChanged(quint64 tagId, bool checked);
  void onTableRowDoubleClicked(const QModelIndex& index);
  void onSelectAllClicked();
  void onSelectNoneClicked();
  void onLeadLagSpinChanged();
  void onApplyLeadLagToAllClicked();
  void onListFixtureChanged(int fixtureId);

private:
  enum class ListFixture { Session, WorstCase, Empty, One, Thousand };

  void buildUi();
  void rebuildEventFilterOptions();
  QVector<PresentationInstancesModel::Row> buildVisibleRows() const;
  QVector<PresentationInstancesModel::Row> buildFixtureRows() const;
  PresentationInstancesModel::Row makeVisibleRow(int tagSessionIndex, quint64 tagId, qint64 markMs,
                                                 const QString& teamKey, const QString& mainEvent,
                                                 const QString& followUpEvent) const;
  void rebuildRows();
  void syncModelSelection();
  void updateSelectionSummary();
  void updateCurrentClipControlsEnabled();
  void updateShowNotesCheckboxVisibility();
  bool selectionHasAnyNotes() const;
  void emitSelectionChanged();
  void pruneSelectionToExistingTags();
  bool resolveCurrentClip(int tagSessionIndex, int* resolvedIndex, quint64* resolvedTagId) const;
  quint64 tagIdAt(int tagSessionIndex) const;
  bool passesFilters(const QString& mainEvent, const QString& team) const;
  bool eventFilterValueIsValid(const QString& eventFilter) const;
  QString teamDisplayName(const QString& teamKey) const;
  QDoubleSpinBox* focusedLeadLagSpinBox() const;
  bool isInsideLeadLagSpinBox(const QWidget* widget) const;
  void releaseLeadLagSpinBoxFocus();

  TagSession* tagSession_ = nullptr;

  QLabel* titleLabel_ = nullptr;
  QLabel* eventFilterLabel_ = nullptr;
  QComboBox* eventFilterCombo_ = nullptr;
  QLabel* teamFilterLabel_ = nullptr;
  QComboBox* teamFilterCombo_ = nullptr;
  QToolButton* selectAllButton_ = nullptr;
  QToolButton* selectNoneButton_ = nullptr;
  QLabel* selectionSummaryLabel_ = nullptr;
  PresentationInstancesModel* instancesModel_ = nullptr;
  QTableView* instancesTable_ = nullptr;

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
  /// Dev-only break-ui dataset. Session is the live tag list. Anything else is
  /// shown only when AVA_BREAK_UI=1 and never writes the presentation queue.
  ListFixture listFixture_ = ListFixture::Session;
  QPointer<QWidget> breakUiToggle_;
  quint64 currentTagId_ = 0;
  int currentTagSessionIndex_ = -1;
  QVector<int> lastEmittedSelectedIndexes_{};
};
