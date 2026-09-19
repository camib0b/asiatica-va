#pragma once

#include "../export/XmlImporter.h"
#include "../state/TagSession.h"

#include <QDialog>
#include <QHash>
#include <QPointer>
#include <QVector>
#include <QtGlobal>

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

private:
  struct MappingRow {
    QString xmlCode;
    int count = 0;
    QPointer<QComboBox> eventCombo;
    QPointer<QComboBox> teamCombo;
    QTableWidgetItem* importItem = nullptr;
  };

  struct ParsedTeamCode {
    QString abbrev;
    QString shortCode;
    QChar sign;
    bool valid = false;
  };

  void buildUi();
  void populateRows();
  void applyAutoMappings();
  void configureMappingTable();
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

  QVector<XmlImporter::ParsedInstance> instances_;
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
  QPointer<QTableWidget> mappingTable_;
  QPointer<QPushButton> importButton_;
  QPointer<QPushButton> cancelButton_;

  QVector<MappingRow> rows_;
  ImportMappingResult importResult_;
};
