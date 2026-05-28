#pragma once

#include "../export/XmlImporter.h"
#include "../state/TagSession.h"

#include <QDialog>
#include <QHash>
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

  XmlEventMappingDialog(const QVector<XmlImporter::ParsedInstance>& instances,
                        qint64 offsetMs,
                        const TagSession* session,
                        QWidget* parent = nullptr);

  QVector<TagSession::GameTag> buildGameTags() const;
  int skippedInstanceCount() const;

  void applyUiStrings();

private slots:
  void onImportClicked();
  void onAbbrevMappingChanged();

private:
  struct MappingRow {
    QString xmlCode;
    int count = 0;
    QComboBox* eventCombo = nullptr;
    QComboBox* teamCombo = nullptr;
    QTableWidgetItem* importItem = nullptr;
    bool autoMapped = false;
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
  bool isRowImportEnabled(int row) const;
  void setRowImportEnabled(int row, bool enabled);
  static ParsedTeamCode parseTeamCodePattern(const QString& code);
  QString teamForAbbrev(const QString& abbrev) const;
  QStringList eventChoices() const;
  CodeMapping mappingForRow(const MappingRow& row) const;
  bool validateMappings(QString* errorMessage) const;
  TagSession::GameTag gameTagFromInstance(const XmlImporter::ParsedInstance& instance,
                                          const CodeMapping& mapping) const;
  void inferPeriods(QVector<TagSession::GameTag>& tags) const;

  QVector<XmlImporter::ParsedInstance> instances_;
  qint64 offsetMs_ = 0;
  const TagSession* session_ = nullptr;
  QString sessionHomeAbbrev_;
  QString sessionAwayAbbrev_;
  QHash<QString, QString> xmlAbbrevToTeamSide_;

  QLabel* titleLabel_ = nullptr;
  QLabel* instructionsLabel_ = nullptr;
  QLabel* abbrevHeaderLabel_ = nullptr;
  QLabel* homeAbbrevLabel_ = nullptr;
  QLabel* awayAbbrevLabel_ = nullptr;
  QComboBox* xmlHomeAbbrevCombo_ = nullptr;
  QComboBox* xmlAwayAbbrevCombo_ = nullptr;
  QTableWidget* mappingTable_ = nullptr;
  QPushButton* importButton_ = nullptr;
  QPushButton* cancelButton_ = nullptr;

  QVector<MappingRow> rows_;
  mutable int skippedInstanceCount_ = 0;
};
