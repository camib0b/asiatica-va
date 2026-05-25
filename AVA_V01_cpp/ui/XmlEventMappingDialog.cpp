#include "XmlEventMappingDialog.h"

#include "../i18n/AppLocale.h"
#include "../state/EventCodeMap.h"
#include "../state/EventDefaults.h"
#include "../style/StyleProps.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

constexpr int kColCode = 0;
constexpr int kColCount = 1;
constexpr int kColEvent = 2;
constexpr int kColTeam = 3;
constexpr int kColAction = 4;

QString skipChoiceValue() { return QStringLiteral("__skip__"); }

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
  setMinimumSize(720, 480);
  resize(800, 520);
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

  auto* abbrevGrid = new QGridLayout();
  abbrevHeaderLabel_ = new QLabel(this);
  xmlHomeAbbrevCombo_ = new QComboBox(this);
  xmlAwayAbbrevCombo_ = new QComboBox(this);
  abbrevGrid->addWidget(abbrevHeaderLabel_, 0, 0, 1, 2);
  abbrevGrid->addWidget(new QLabel(AppLocale::trUi("xml_import.mapping_home_abbrev"), this), 1, 0);
  abbrevGrid->addWidget(xmlHomeAbbrevCombo_, 1, 1);
  abbrevGrid->addWidget(new QLabel(AppLocale::trUi("xml_import.mapping_away_abbrev"), this), 2, 0);
  abbrevGrid->addWidget(xmlAwayAbbrevCombo_, 2, 1);
  layout->addLayout(abbrevGrid);

  mappingTable_ = new QTableWidget(this);
  mappingTable_->setColumnCount(5);
  mappingTable_->horizontalHeader()->setStretchLastSection(true);
  mappingTable_->horizontalHeader()->setSectionResizeMode(kColCode, QHeaderView::Stretch);
  mappingTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
  mappingTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
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

    mappingRow.eventCombo = new QComboBox(mappingTable_);
    mappingRow.eventCombo->addItems(events);
    mappingTable_->setCellWidget(row, kColEvent, mappingRow.eventCombo);

    mappingRow.teamCombo = new QComboBox(mappingTable_);
    mappingRow.teamCombo->addItem(AppLocale::trUi("xml_import.mapping_team_none"), QString());
    mappingRow.teamCombo->addItem(AppLocale::trUi("export.team_home_default"), QStringLiteral("Home"));
    mappingRow.teamCombo->addItem(AppLocale::trUi("export.team_away_default"), QStringLiteral("Away"));
    mappingTable_->setCellWidget(row, kColTeam, mappingRow.teamCombo);

    mappingRow.actionCombo = new QComboBox(mappingTable_);
    mappingRow.actionCombo->addItem(AppLocale::trUi("xml_import.mapping_action_import"),
                                    QStringLiteral("import"));
    mappingRow.actionCombo->addItem(AppLocale::trUi("xml_import.mapping_action_skip"),
                                    skipChoiceValue());
    mappingTable_->setCellWidget(row, kColAction, mappingRow.actionCombo);

    rows_.append(mappingRow);
  }

  onAbbrevMappingChanged();
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

  for (int i = 0; i < rows_.size(); ++i) {
    MappingRow& row = rows_[i];
    if (!row.eventCombo || !row.teamCombo || !row.actionCombo) continue;

    const QString code = row.xmlCode;

    if (EventDefaults::isTimeControlEvent(code)) {
      const int eventIndex = row.eventCombo->findText(code);
      if (eventIndex >= 0) row.eventCombo->setCurrentIndex(eventIndex);
      row.teamCombo->setCurrentIndex(0);
      row.actionCombo->setCurrentIndex(0);
      row.autoMapped = true;
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
          row.actionCombo->setCurrentIndex(1);
          row.autoMapped = true;
          continue;
        }
      }

      if (parsed.sign == QLatin1Char('+')) {
        const QString team = teamForAbbrev(parsed.abbrev);
        if (!team.isEmpty()) {
          const int teamIndex = row.teamCombo->findData(team);
          if (teamIndex >= 0) row.teamCombo->setCurrentIndex(teamIndex);
          row.actionCombo->setCurrentIndex(0);
          row.autoMapped = true;
          continue;
        }
      }
    }

    row.autoMapped = false;
  }
}

XmlEventMappingDialog::CodeMapping XmlEventMappingDialog::mappingForRow(
    const MappingRow& row) const {
  CodeMapping mapping;
  mapping.xmlCode = row.xmlCode;
  if (row.actionCombo &&
      row.actionCombo->currentData().toString() == skipChoiceValue()) {
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
  importButton_->setText(AppLocale::trUi("xml_import.import"));
  cancelButton_->setText(AppLocale::trUi("xml_import.cancel"));

  mappingTable_->setHorizontalHeaderLabels({
      AppLocale::trUi("xml_import.mapping_col_code"),
      AppLocale::trUi("xml_import.mapping_col_count"),
      AppLocale::trUi("xml_import.mapping_col_event"),
      AppLocale::trUi("xml_import.mapping_col_team"),
      AppLocale::trUi("xml_import.mapping_col_action"),
  });
}
