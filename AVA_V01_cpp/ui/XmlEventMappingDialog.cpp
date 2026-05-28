#include "XmlEventMappingDialog.h"

#include "../i18n/AppLocale.h"
#include "../state/EventCodeMap.h"
#include "../state/EventDefaults.h"
#include "../style/StyleProps.h"

#include <QAbstractItemView>
#include <QColor>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace {

constexpr int kColCode = 0;
constexpr int kColCount = 1;
constexpr int kColEvent = 2;
constexpr int kColTeam = 3;
constexpr int kColImport = 4;

const QColor kActiveRowBgEven(0xFFFFFF);
const QColor kActiveRowBgOdd(0xFAFAFA);
const QColor kInactiveRowBg(0xF4F4F5);
const QColor kActiveText(0x09090B);
const QColor kInactiveText(0xA1A1AA);

QComboBox* makeAbbrevComboWidget(QWidget* parent) {
  auto* combo = new QComboBox(parent);
  Style::setVariant(combo, "abbrev");
  combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  combo->setMaximumWidth(72);
  combo->setMinimumWidth(56);
  return combo;
}

QComboBox* makeTableComboWidget(QWidget* parent) {
  auto* combo = new QComboBox(parent);
  Style::setVariant(combo, "compact");
  combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  combo->setFixedHeight(28);
  return combo;
}

void embedTableCombo(QTableWidget* table, int row, int column, QComboBox* combo) {
  auto* cellHost = new QWidget(table);
  auto* cellLayout = new QHBoxLayout(cellHost);
  cellLayout->setContentsMargins(4, 2, 4, 2);
  cellLayout->setSpacing(0);
  cellLayout->addWidget(combo);
  table->setCellWidget(row, column, cellHost);
}

QTableWidgetItem* makeImportCheckItem() {
  auto* item = new QTableWidgetItem();
  item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
  item->setCheckState(Qt::Checked);
  item->setTextAlignment(Qt::AlignCenter);
  return item;
}

} // namespace

XmlEventMappingDialog::XmlEventMappingDialog(const QVector<XmlImporter::ParsedInstance>& instances,
                                             qint64 offsetMs,
                                             const TagSession* session,
                                             QWidget* parent)
    : QDialog(parent), instances_(instances), offsetMs_(offsetMs), session_(session) {
  if (session_) {
    sessionHomeAbbrev_ = session_->homeAbbrev();
    sessionAwayAbbrev_ = session_->awayAbbrev();
  }
  setWindowTitle(AppLocale::trUi("xml_import.mapping_title"));
  setObjectName(QStringLiteral("XmlEventMappingDialog"));
  setMinimumSize(760, 640);
  resize(820, 780);
  buildUi();
  populateRows();
  applyAutoMappings();
  applyUiStrings();
}

void XmlEventMappingDialog::buildUi() {
  auto* layout = new QVBoxLayout(this);
  layout->setSpacing(10);

  titleLabel_ = new QLabel(this);
  Style::setRole(titleLabel_, "h2");
  layout->addWidget(titleLabel_);

  instructionsLabel_ = new QLabel(this);
  instructionsLabel_->setWordWrap(true);
  layout->addWidget(instructionsLabel_);

  abbrevHeaderLabel_ = new QLabel(this);
  layout->addWidget(abbrevHeaderLabel_);

  auto* abbrevRow = new QHBoxLayout();
  abbrevRow->setSpacing(8);
  homeAbbrevLabel_ = new QLabel(this);
  xmlHomeAbbrevCombo_ = makeAbbrevComboWidget(this);
  awayAbbrevLabel_ = new QLabel(this);
  xmlAwayAbbrevCombo_ = makeAbbrevComboWidget(this);
  abbrevRow->addWidget(homeAbbrevLabel_);
  abbrevRow->addWidget(xmlHomeAbbrevCombo_);
  abbrevRow->addSpacing(20);
  abbrevRow->addWidget(awayAbbrevLabel_);
  abbrevRow->addWidget(xmlAwayAbbrevCombo_);
  abbrevRow->addStretch();
  layout->addLayout(abbrevRow);

  mappingTable_ = new QTableWidget(this);
  mappingTable_->setObjectName(QStringLiteral("XmlEventMappingTable"));
  mappingTable_->setColumnCount(5);
  mappingTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
  mappingTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  mappingTable_->setAlternatingRowColors(false);
  layout->addWidget(mappingTable_, 1);

  auto* buttonRow = new QHBoxLayout();
  buttonRow->addStretch();
  cancelButton_ = new QPushButton(this);
  importButton_ = new QPushButton(this);
  Style::setVariant(importButton_, "primary");
  importButton_->setDefault(true);
  buttonRow->addWidget(cancelButton_);
  buttonRow->addWidget(importButton_);
  layout->addLayout(buttonRow);

  connect(cancelButton_, &QPushButton::clicked, this, &QDialog::reject);
  connect(importButton_, &QPushButton::clicked, this, &XmlEventMappingDialog::onImportClicked);
  connect(xmlHomeAbbrevCombo_, &QComboBox::currentTextChanged, this,
          [this](const QString&) {
            onAbbrevMappingChanged();
            applyAutoMappings();
          });
  connect(xmlAwayAbbrevCombo_, &QComboBox::currentTextChanged, this,
          [this](const QString&) {
            onAbbrevMappingChanged();
            applyAutoMappings();
          });
  connect(mappingTable_, &QTableWidget::itemChanged, this,
          [this](QTableWidgetItem* item) {
            if (!item || item->column() != kColImport) return;
            updateRowImportState(item->row());
          });
}

void XmlEventMappingDialog::populateRows() {
  QHash<QString, int> codeCounts;
  QSet<QString> detectedAbbrevs;
  for (const XmlImporter::ParsedInstance& instance : instances_) {
    codeCounts[instance.code] = codeCounts.value(instance.code, 0) + 1;
    const ParsedTeamCode parsed = parseTeamCodePattern(instance.code);
    if (parsed.valid && parsed.sign == QLatin1Char('+')) {
      detectedAbbrevs.insert(parsed.abbrev);
    }
  }

  QStringList abbrevChoices = detectedAbbrevs.values();
  abbrevChoices.sort(Qt::CaseInsensitive);
  if (!sessionHomeAbbrev_.isEmpty() && !abbrevChoices.contains(sessionHomeAbbrev_)) {
    abbrevChoices.prepend(sessionHomeAbbrev_);
  }
  if (!sessionAwayAbbrev_.isEmpty() && !abbrevChoices.contains(sessionAwayAbbrev_)) {
    abbrevChoices.prepend(sessionAwayAbbrev_);
  }
  if (abbrevChoices.isEmpty()) {
    abbrevChoices << sessionHomeAbbrev_ << sessionAwayAbbrev_;
    abbrevChoices.removeAll(QString());
  }

  xmlHomeAbbrevCombo_->addItems(abbrevChoices);
  xmlAwayAbbrevCombo_->addItems(abbrevChoices);
  if (!sessionHomeAbbrev_.isEmpty()) {
    const int homeIndex = xmlHomeAbbrevCombo_->findText(sessionHomeAbbrev_, Qt::MatchFixedString);
    if (homeIndex >= 0) xmlHomeAbbrevCombo_->setCurrentIndex(homeIndex);
  }
  if (!sessionAwayAbbrev_.isEmpty()) {
    const int awayIndex = xmlAwayAbbrevCombo_->findText(sessionAwayAbbrev_, Qt::MatchFixedString);
    if (awayIndex >= 0) xmlAwayAbbrevCombo_->setCurrentIndex(awayIndex);
  }

  QStringList sortedCodes = codeCounts.keys();
  sortedCodes.sort(Qt::CaseInsensitive);

  mappingTable_->setRowCount(sortedCodes.size());
  rows_.clear();
  rows_.reserve(sortedCodes.size());

  const QStringList events = eventChoices();

  for (int row = 0; row < sortedCodes.size(); ++row) {
    const QString code = sortedCodes.at(row);
    MappingRow mappingRow;
    mappingRow.xmlCode = code;
    mappingRow.count = codeCounts.value(code);

    auto* codeItem = new QTableWidgetItem(code);
    mappingTable_->setItem(row, kColCode, codeItem);
    mappingTable_->setItem(row, kColCount, new QTableWidgetItem(QString::number(mappingRow.count)));

    mappingRow.eventCombo = makeTableComboWidget(mappingTable_);
    mappingRow.eventCombo->addItems(events);
    embedTableCombo(mappingTable_, row, kColEvent, mappingRow.eventCombo);

    mappingRow.teamCombo = makeTableComboWidget(mappingTable_);
    mappingRow.teamCombo->addItem(AppLocale::trUi("xml_import.mapping_team_none"), QString());
    mappingRow.teamCombo->addItem(AppLocale::trUi("export.team_home_default"), QStringLiteral("Home"));
    mappingRow.teamCombo->addItem(AppLocale::trUi("export.team_away_default"), QStringLiteral("Away"));
    embedTableCombo(mappingTable_, row, kColTeam, mappingRow.teamCombo);

    mappingRow.importItem = makeImportCheckItem();
    mappingTable_->setItem(row, kColImport, mappingRow.importItem);

    rows_.append(mappingRow);
  }

  configureMappingTable();
  onAbbrevMappingChanged();
}

void XmlEventMappingDialog::configureMappingTable() {
  if (!mappingTable_) return;

  mappingTable_->verticalHeader()->setVisible(false);
  mappingTable_->verticalHeader()->setDefaultSectionSize(34);
  mappingTable_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);

  QHeaderView* header = mappingTable_->horizontalHeader();
  header->setStretchLastSection(false);
  header->setSectionResizeMode(kColCode, QHeaderView::Stretch);
  header->setSectionResizeMode(kColEvent, QHeaderView::Stretch);
  header->setSectionResizeMode(kColCount, QHeaderView::ResizeToContents);
  header->setSectionResizeMode(kColTeam, QHeaderView::ResizeToContents);
  header->setSectionResizeMode(kColImport, QHeaderView::Fixed);
  mappingTable_->setColumnWidth(kColImport, 56);
  header->setMinimumSectionSize(72);
}

bool XmlEventMappingDialog::isRowImportEnabled(int row) const {
  if (row < 0 || row >= rows_.size()) return false;
  const QTableWidgetItem* importItem = rows_.at(row).importItem;
  return importItem && importItem->checkState() == Qt::Checked;
}

void XmlEventMappingDialog::setRowImportEnabled(int row, bool enabled) {
  if (row < 0 || row >= rows_.size()) return;
  MappingRow& mappingRow = rows_[row];
  if (!mappingRow.importItem) return;

  QSignalBlocker blocker(mappingTable_);
  mappingRow.importItem->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
  updateRowImportState(row);
}

void XmlEventMappingDialog::updateRowImportState(int row) {
  if (!mappingTable_ || row < 0 || row >= rows_.size()) return;

  const bool importing = isRowImportEnabled(row);
  const QColor rowBackground = importing
                                   ? (row % 2 == 0 ? kActiveRowBgEven : kActiveRowBgOdd)
                                   : kInactiveRowBg;
  const QColor rowForeground = importing ? kActiveText : kInactiveText;

  for (int column = 0; column < mappingTable_->columnCount(); ++column) {
    if (QTableWidgetItem* item = mappingTable_->item(row, column)) {
      item->setBackground(rowBackground);
      if (column != kColImport) {
        item->setForeground(rowForeground);
      }
    }
  }

  MappingRow& mappingRow = rows_[row];
  if (mappingRow.eventCombo) mappingRow.eventCombo->setEnabled(importing);
  if (mappingRow.teamCombo) mappingRow.teamCombo->setEnabled(importing);
}

QStringList XmlEventMappingDialog::eventChoices() const {
  QStringList events = EventDefaults::allConfigurableEventTypes();
  events << QString::fromLatin1(EventDefaults::TimeCodes::kStartAnchor)
         << QString::fromLatin1(EventDefaults::TimeCodes::kTimeout)
         << QString::fromLatin1(EventDefaults::TimeCodes::kQuarter1)
         << QString::fromLatin1(EventDefaults::TimeCodes::kQuarter2)
         << QString::fromLatin1(EventDefaults::TimeCodes::kQuarter3)
         << QString::fromLatin1(EventDefaults::TimeCodes::kQuarter4);
  events.removeDuplicates();
  return events;
}

XmlEventMappingDialog::ParsedTeamCode XmlEventMappingDialog::parseTeamCodePattern(
    const QString& code) {
  ParsedTeamCode result;
  const QString trimmed = code.trimmed();
  if (trimmed.length() < 3) return result;

  const QChar lastChar = trimmed.at(trimmed.length() - 1);
  if (lastChar != QLatin1Char('+') && lastChar != QLatin1Char('-')) return result;

  const int spaceIndex = trimmed.lastIndexOf(QLatin1Char(' '));
  if (spaceIndex <= 0 || spaceIndex >= trimmed.length() - 2) return result;

  result.abbrev = trimmed.left(spaceIndex).trimmed().toUpper();
  result.shortCode = trimmed.mid(spaceIndex + 1, trimmed.length() - spaceIndex - 2).trimmed().toUpper();
  result.sign = lastChar;
  result.valid = !result.abbrev.isEmpty() && !result.shortCode.isEmpty();
  return result;
}

void XmlEventMappingDialog::onAbbrevMappingChanged() {
  xmlAbbrevToTeamSide_.clear();
  const QString homeAbbrev = xmlHomeAbbrevCombo_->currentText().trimmed().toUpper();
  const QString awayAbbrev = xmlAwayAbbrevCombo_->currentText().trimmed().toUpper();
  if (!homeAbbrev.isEmpty()) xmlAbbrevToTeamSide_.insert(homeAbbrev, QStringLiteral("Home"));
  if (!awayAbbrev.isEmpty()) xmlAbbrevToTeamSide_.insert(awayAbbrev, QStringLiteral("Away"));
}

QString XmlEventMappingDialog::teamForAbbrev(const QString& abbrev) const {
  return xmlAbbrevToTeamSide_.value(abbrev.trimmed().toUpper());
}

void XmlEventMappingDialog::applyAutoMappings() {
  QSet<QString> positiveCodes;
  for (const MappingRow& row : rows_) {
    const ParsedTeamCode parsed = parseTeamCodePattern(row.xmlCode);
    if (parsed.valid && parsed.sign == QLatin1Char('+')) {
      positiveCodes.insert(row.xmlCode);
    }
  }

  QSignalBlocker blocker(mappingTable_);
  for (int i = 0; i < rows_.size(); ++i) {
    MappingRow& row = rows_[i];
    if (!row.eventCombo || !row.teamCombo || !row.importItem) continue;

    const QString code = row.xmlCode;
    bool importEnabled = true;

    if (EventDefaults::isTimeControlEvent(code)) {
      const int eventIndex = row.eventCombo->findText(code);
      if (eventIndex >= 0) row.eventCombo->setCurrentIndex(eventIndex);
      row.teamCombo->setCurrentIndex(0);
      row.autoMapped = true;
      setRowImportEnabled(i, true);
      continue;
    }

    const ParsedTeamCode parsed = parseTeamCodePattern(code);
    if (parsed.valid) {
      const QString mainEvent = EventCodeMap::mainEventForShortCode(parsed.shortCode);
      if (!mainEvent.isEmpty()) {
        const int eventIndex = row.eventCombo->findText(mainEvent);
        if (eventIndex >= 0) row.eventCombo->setCurrentIndex(eventIndex);
      }

      if (parsed.sign == QLatin1Char('-')) {
        const QString positiveCode =
            QStringLiteral("%1 %2+").arg(parsed.abbrev, parsed.shortCode);
        if (positiveCodes.contains(positiveCode)) {
          importEnabled = false;
          row.autoMapped = true;
          setRowImportEnabled(i, importEnabled);
          continue;
        }
      }

      if (parsed.sign == QLatin1Char('+')) {
        const QString team = teamForAbbrev(parsed.abbrev);
        if (!team.isEmpty()) {
          const int teamIndex = row.teamCombo->findData(team);
          if (teamIndex >= 0) row.teamCombo->setCurrentIndex(teamIndex);
          row.autoMapped = true;
          setRowImportEnabled(i, true);
          continue;
        }
      }
    }

    row.autoMapped = false;
    setRowImportEnabled(i, importEnabled);
  }
}

XmlEventMappingDialog::CodeMapping XmlEventMappingDialog::mappingForRow(
    const MappingRow& row) const {
  CodeMapping mapping;
  mapping.xmlCode = row.xmlCode;
  if (!row.importItem || row.importItem->checkState() != Qt::Checked) {
    mapping.skip = true;
    return mapping;
  }
  if (row.eventCombo) mapping.canonicalMainEvent = row.eventCombo->currentText();
  if (row.teamCombo) mapping.team = row.teamCombo->currentData().toString();
  mapping.skip = mapping.canonicalMainEvent.isEmpty();
  return mapping;
}

bool XmlEventMappingDialog::validateMappings(QString* errorMessage) const {
  QHash<QString, CodeMapping> mappingByCode;
  for (const MappingRow& row : rows_) {
    mappingByCode.insert(row.xmlCode, mappingForRow(row));
  }

  int importableCount = 0;
  for (const XmlImporter::ParsedInstance& instance : instances_) {
    const CodeMapping mapping = mappingByCode.value(instance.code);
    if (!mapping.skip) ++importableCount;
  }

  if (importableCount == 0) {
    if (errorMessage) *errorMessage = AppLocale::trUi("xml_import.mapping_none_selected");
    return false;
  }

  for (const MappingRow& row : rows_) {
    const CodeMapping mapping = mappingForRow(row);
    if (mapping.skip) continue;
    if (mapping.canonicalMainEvent.isEmpty()) {
      if (errorMessage) {
        *errorMessage = AppLocale::trUi("xml_import.mapping_missing_event").arg(row.xmlCode);
      }
      return false;
    }
    if (!EventDefaults::isTimeControlEvent(mapping.canonicalMainEvent) && mapping.team.isEmpty()) {
      if (errorMessage) {
        *errorMessage = AppLocale::trUi("xml_import.mapping_missing_team").arg(row.xmlCode);
      }
      return false;
    }
  }

  return true;
}

TagSession::GameTag XmlEventMappingDialog::gameTagFromInstance(
    const XmlImporter::ParsedInstance& instance,
    const CodeMapping& mapping) const {
  TagSession::GameTag tag;
  tag.mainEvent = mapping.canonicalMainEvent;
  tag.team = mapping.team;
  tag.period = instance.periodLabel;
  tag.intervalManuallyEdited = true;

  tag.startMs = instance.startMs + offsetMs_;
  tag.endMs = instance.endMs + offsetMs_;
  if (tag.startMs < 0) tag.startMs = 0;
  if (tag.endMs < tag.startMs) tag.endMs = tag.startMs;

  if (EventDefaults::isTimeControlEvent(tag.mainEvent)) {
    tag.positionMs = tag.startMs;
  } else {
    const EventDefaults::EventDuration duration = EventDefaults::defaultFor(tag.mainEvent);
    tag.positionMs = tag.startMs + duration.preMs;
    if (tag.positionMs > tag.endMs) tag.positionMs = tag.endMs;
    if (tag.positionMs < tag.startMs) tag.positionMs = tag.startMs;
  }

  return tag;
}

void XmlEventMappingDialog::inferPeriods(QVector<TagSession::GameTag>& tags) const {
  struct QuarterSpan {
    QString label;
    qint64 startMs = 0;
    qint64 endMs = 0;
  };
  QVector<QuarterSpan> quarterSpans;
  for (const TagSession::GameTag& tag : tags) {
    if (!EventDefaults::isQuarterEvent(tag.mainEvent)) continue;
    quarterSpans.append({tag.mainEvent, tag.startMs, tag.endMs});
  }

  for (TagSession::GameTag& tag : tags) {
    if (!tag.period.isEmpty()) continue;
    if (EventDefaults::isTimeControlEvent(tag.mainEvent)) {
      if (EventDefaults::isQuarterEvent(tag.mainEvent)) {
        tag.period = tag.mainEvent;
      }
      continue;
    }
    for (const QuarterSpan& span : quarterSpans) {
      if (tag.positionMs >= span.startMs && tag.positionMs <= span.endMs) {
        tag.period = span.label;
        break;
      }
    }
  }
}

QVector<TagSession::GameTag> XmlEventMappingDialog::buildGameTags() const {
  skippedInstanceCount_ = 0;
  QHash<QString, CodeMapping> mappingByCode;
  for (const MappingRow& row : rows_) {
    mappingByCode.insert(row.xmlCode, mappingForRow(row));
  }

  QVector<TagSession::GameTag> tags;
  tags.reserve(instances_.size());
  for (const XmlImporter::ParsedInstance& instance : instances_) {
    const CodeMapping mapping = mappingByCode.value(instance.code);
    if (mapping.skip) {
      ++skippedInstanceCount_;
      continue;
    }
    tags.append(gameTagFromInstance(instance, mapping));
  }

  inferPeriods(tags);
  return tags;
}

int XmlEventMappingDialog::skippedInstanceCount() const {
  return skippedInstanceCount_;
}

void XmlEventMappingDialog::onImportClicked() {
  QString errorMessage;
  if (!validateMappings(&errorMessage)) {
    if (!errorMessage.isEmpty()) {
      QMessageBox::warning(this, AppLocale::trUi("xml_import.mapping_title"), errorMessage);
    }
    return;
  }
  accept();
}

void XmlEventMappingDialog::applyUiStrings() {
  titleLabel_->setText(AppLocale::trUi("xml_import.mapping_title"));
  instructionsLabel_->setText(AppLocale::trUi("xml_import.mapping_instructions"));
  abbrevHeaderLabel_->setText(AppLocale::trUi("xml_import.mapping_abbrev_header"));
  if (homeAbbrevLabel_) {
    homeAbbrevLabel_->setText(AppLocale::trUi("xml_import.mapping_home_abbrev"));
  }
  if (awayAbbrevLabel_) {
    awayAbbrevLabel_->setText(AppLocale::trUi("xml_import.mapping_away_abbrev"));
  }
  importButton_->setText(AppLocale::trUi("xml_import.import"));
  cancelButton_->setText(AppLocale::trUi("xml_import.cancel"));

  mappingTable_->setHorizontalHeaderLabels({
      AppLocale::trUi("xml_import.mapping_col_code"),
      AppLocale::trUi("xml_import.mapping_col_count"),
      AppLocale::trUi("xml_import.mapping_col_event"),
      AppLocale::trUi("xml_import.mapping_col_team"),
      AppLocale::trUi("xml_import.mapping_col_import"),
  });
}
