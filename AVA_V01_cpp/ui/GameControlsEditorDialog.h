#pragma once

#include "../state/EventCatalog.h"
#include "../state/EventLabelOverrides.h"

#include <QDialog>
#include <QHash>
#include <QLayout>
#include <QPointer>
#include <QString>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSplitter;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;
class TagSession;

/// Full-screen editor for the open match's control setup.
///
/// The grid matches the tagging keyboard. The selected key's follow-ups are an
/// indented tree beside it. Save writes the draft through EventControlsSetup.
/// Templates are the named setups from EventControlsTemplateStore. Cancel drops
/// the draft. There is no edit-in-place path on the live tagging grid.
class GameControlsEditorDialog final : public QDialog {
  Q_OBJECT

 public:
  /// \p templateStorageRoot empty uses the application data directory.
  explicit GameControlsEditorDialog(TagSession* session, QWidget* parent = nullptr,
                                    const QString& templateStorageRoot = QString());

  void applyUiStrings();

  /// Demo / worst / empty / one. The dev toggle and tests share this.
  enum class DataFixture {
    Session,
    WorstCase,
    EmptyFollowUps,
    OneFollowUp,
  };
  void applyDataFixture(DataFixture fixture);

 protected:
  void showEvent(QShowEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  void buildUi();
  void buildTemplateBar(QLayout* rootLayout);
  void buildEditorBody(QLayout* rootLayout);
  void buildGrid(QWidget* host);
  void buildDetail(QWidget* host);
  void buildBreakToggle();
  void reloadTemplateList(const QString& selectedStableName);
  void selectEvent(EventCatalog::EventId eventId, bool force);
  void loadDetail();
  void onNameEdited(const QString& text);
  void onKeyEdited(const QString& text);
  void onShownToggled(bool shown);
  void onFollowUpItemChanged(QTreeWidgetItem* item, int column);
  void onSaveClicked();
  void onResetClicked();
  void onLoadTemplateClicked();
  void onSaveTemplateClicked();
  void onRemoveTemplateClicked();
  void updateGridButton(QPushButton* button);
  QPushButton* gridButtonFor(EventCatalog::EventId eventId) const;
  void updateAllGridButtons();
  void updateGridSelection();
  void refitButton(QPushButton* button);
  void refitGridLabels();
  void rebuildFollowUpTree();
  void updateFollowUpSummary();
  void updateBanner();
  void updateSaveEnabled();
  void updateRemoveTemplateEnabled();
  void positionBreakToggle();
  bool canSave() const;
  bool currentEventIsClean() const;
  bool draftDiffersFromSession() const;
  void rememberPendingLabel(EventCatalog::EventId eventId, const QString& text, bool pending);
  void rememberPendingKey(EventCatalog::EventId eventId, const QString& text, bool pending);

  TagSession* tagSession_ = nullptr;
  QString templateStorageRoot_;
  EventLabelOverrides::Table draft_;
  EventCatalog::EventId selectedEventId_ = EventCatalog::EventId::None;
  QHash<int, QString> pendingLabels_;
  QHash<int, QString> pendingKeys_;
  bool suppressDetailEdits_ = false;

  QPointer<QLabel> titleLabel_;
  QPointer<QLabel> subtitleLabel_;
  QPointer<QLabel> templateLabel_;
  QPointer<QComboBox> templateCombo_;
  QPointer<QPushButton> loadButton_;
  QPointer<QPushButton> removeButton_;
  QPointer<QLabel> templateNameLabel_;
  QPointer<QLineEdit> templateNameEdit_;
  QPointer<QPushButton> saveTemplateButton_;
  QPointer<QLabel> templateStatus_;
  QPointer<QLabel> gridCaption_;
  QPointer<QWidget> gridHost_;
  QPointer<QLabel> nameLabel_;
  QPointer<QLineEdit> nameEdit_;
  QPointer<QLabel> nameError_;
  QPointer<QLabel> keyLabel_;
  QPointer<QLineEdit> keyEdit_;
  QPointer<QLabel> keyError_;
  QPointer<QCheckBox> shownBox_;
  QPointer<QLabel> followUpHeading_;
  QPointer<QLabel> followUpCount_;
  QPointer<QLabel> followUpEmpty_;
  QPointer<QTreeWidget> followUpTree_;
  QPointer<QLabel> followUpDetail_;
  QPointer<QLabel> banner_;
  QPointer<QPushButton> resetButton_;
  QPointer<QPushButton> cancelButton_;
  QPointer<QPushButton> saveButton_;
  QPointer<QWidget> breakToggle_;
  QPointer<QSplitter> splitter_;
  QVector<QPointer<QPushButton>> gridButtons_;
  QHash<int, QPointer<QPushButton>> breakSegmentButtons_;
};
