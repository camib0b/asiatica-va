#pragma once

#include "../export/XmlImporter.h"
#include "../state/TagSession.h"

#include <QDialog>
#include <QHash>
#include <QPointer>
#include <QVector>
#include <QtGlobal>

#include <Qt>

class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;

class XmlEventMappingDialog final : public QDialog {
  Q_OBJECT

public:
  struct CodeMapping {
    QString xmlCode;
    QString canonicalMainEvent;
    QString team;
    bool skip = false;
  };

  struct ImportMappingResult {
    QVector<TagSession::GameTag> tags;
    int skippedInstanceCount = 0;
    /// Instances whose start was before the video and were shifted or trimmed.
    int adjustedBeforeStartCount = 0;
  };

  XmlEventMappingDialog(const QVector<XmlImporter::ParsedInstance>& instances,
                        qint64 offsetMs,
                        const TagSession* session,
                        QWidget* parent = nullptr);

  ImportMappingResult importResult() const { return importResult_; }

  void applyUiStrings();

private slots:
  void onImportClicked();
  void onAbbrevMappingChanged();
  void onSelectAllImportClicked();
  void onDeselectAllImportClicked();
  void onMappingHeaderClicked(int logicalIndex);

private:
  bool abbreviationsCurrentlyConflict() const;
  void updateAbbrevConflictState();

  struct MappingRow {
    QString xmlCode;
    int count = 0;
    QString cachedEvent;
    QString cachedTeam;
    QString cachedTeamLabel;
    bool cachedImport = true;
    QPointer<QComboBox> eventCombo;
    QPointer<QComboBox> teamCombo;
    QTableWidgetItem* importItem = nullptr;
  };

  enum class MappingDataFixture {
    Session,
    WorstCase,
    Empty,
    One,
    Thousand,
  };

  struct ParsedTeamCode {
    QString abbrev;
    QString shortCode;
    QChar sign;
    bool valid = false;
  };

  void buildUi();
  void populateRows();
  void clearMappingTableUi();
  void rebuildMappingTableFromRows();
  void cacheRowWidgetState(int row);
  void cacheAllRowWidgetState();
  void applyAutoMappings();
  void configureMappingTable();
  void sortMappingRows(int column, Qt::SortOrder order);
  void applyMappingDataFixture(MappingDataFixture fixture);
  QVector<XmlImporter::ParsedInstance> instancesForFixture(MappingDataFixture fixture) const;
  bool mappingBreakUiEnabled() const;
  void updateRowImportState(int row);
  void refreshAllRowImportStates();
  bool isRowImportEnabled(int row) const;
  void setRowImportEnabled(int row, bool enabled);
  static ParsedTeamCode parseTeamCodePattern(const QString& code);
  QString teamForAbbrev(const QString& abbrev) const;
  QStringList eventChoices() const;
  CodeMapping mappingForRow(const MappingRow& row) const;
  QHash<QString, CodeMapping> buildMappingByCode() const;
  ImportMappingResult buildImportSnapshot() const;
  bool validateMappings(QString* errorMessage) const;
  TagSession::GameTag gameTagFromInstance(const XmlImporter::ParsedInstance& instance,
                                          const CodeMapping& mapping) const;

  QVector<XmlImporter::ParsedInstance> sessionInstances_;
  QVector<XmlImporter::ParsedInstance> instances_;
  int sortColumn_ = 0;
  Qt::SortOrder sortOrder_ = Qt::AscendingOrder;
  qint64 offsetMs_ = 0;
  const TagSession* session_ = nullptr;
  QString sessionHomeAbbrev_;
  QString sessionAwayAbbrev_;
  QHash<QString, QString> xmlAbbrevToTeamSide_;

  QPointer<QLabel> titleLabel_;
  QPointer<QLabel> instructionsLabel_;
  QPointer<QLabel> abbrevHeaderLabel_;
  QPointer<QLabel> homeAbbrevLabel_;
  QPointer<QLabel> awayAbbrevLabel_;
  QPointer<QComboBox> xmlHomeAbbrevCombo_;
  QPointer<QComboBox> xmlAwayAbbrevCombo_;
  QPointer<QLabel> abbrevConflictLabel_;
  QPointer<QTableWidget> mappingTable_;
  QPointer<QPushButton> selectAllImportButton_;
  QPointer<QPushButton> deselectAllImportButton_;
  QPointer<QWidget> breakUiToggle_;
  QPointer<QPushButton> importButton_;
  QPointer<QPushButton> cancelButton_;

  QVector<MappingRow> rows_;
  ImportMappingResult importResult_;
};
