#include "XmlEventMappingDialog.h"
#include "QtPtr.h"

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

#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <optional>

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

struct CaseInsensitiveLess {
  bool operator()(const QString& left, const QString& right) const {
    return QString::compare(left, right, Qt::CaseInsensitive) < 0;
  }
};

bool abbrevListContains(const QStringList& choices, const QString& abbrev) {
  return std::any_of(choices.cbegin(), choices.cend(), [&](const QString& existing) {
    return QString::compare(existing, abbrev, Qt::CaseInsensitive) == 0;
  });
}

void prependAbbrevIfAbsent(QStringList& choices, const QString& abbrev) {
  if (abbrev.isEmpty() || abbrevListContains(choices, abbrev)) return;
  choices.prepend(abbrev);
}

QString teamSideDisplayLabel(const QString& teamKey,
                             const QString& sessionHomeAbbrev,
                             const QString& sessionAwayAbbrev) {
  if (teamKey == QStringLiteral("Home")) {
    if (!sessionHomeAbbrev.isEmpty()) return sessionHomeAbbrev;
    return AppLocale::trUi("export.team_home_default");
  }
  if (teamKey == QStringLiteral("Away")) {
    if (!sessionAwayAbbrev.isEmpty()) return sessionAwayAbbrev;
    return AppLocale::trUi("export.team_away_default");
  }
  return QString();
}

QString abbrevMappingLabel(const QString& sessionAbbrev, const char* defaultTranslationKey) {
  if (sessionAbbrev.isEmpty()) return AppLocale::trUi(defaultTranslationKey);
  return AppLocale::trUi("xml_import.mapping_abbrev_for_team").arg(sessionAbbrev);
}

void applyTeamComboLabels(QComboBox* teamCombo,
                          const QString& homeLabel,
                          const QString& awayLabel) {
  if (!teamCombo) return;
  QSignalBlocker blocker(teamCombo);
  const int noneIndex = teamCombo->findData(QString());
  if (noneIndex >= 0) {
    teamCombo->setItemText(noneIndex, AppLocale::trUi("xml_import.mapping_team_none"));
  }
  const int homeIndex = teamCombo->findData(QStringLiteral("Home"));
  if (homeIndex >= 0) teamCombo->setItemText(homeIndex, homeLabel);
  const int awayIndex = teamCombo->findData(QStringLiteral("Away"));
  if (awayIndex >= 0) teamCombo->setItemText(awayIndex, awayLabel);
}

std::unique_ptr<QComboBox, QtParentDeleter> makeAbbrevComboWidget(QWidget* parent) {
  auto combo = makeQtPtr<QComboBox>(parent);
  Style::setVariant(combo.get(), "abbrev");
  combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  combo->setMaximumWidth(72);
  combo->setMinimumWidth(56);
  return combo;
}

std::unique_ptr<QComboBox, QtParentDeleter> makeTableComboWidget(QWidget* parent) {
  auto combo = makeQtPtr<QComboBox>(parent);
  Style::setVariant(combo.get(), "compact");
  combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  combo->setFixedHeight(28);
  return combo;
}

void embedTableCombo(QTableWidget* table, int row, int column, QComboBox* combo) {
  auto cellHost = makeQtPtr<QWidget>(table);
  auto cellLayout = makeQtPtr<QHBoxLayout>(cellHost.get());
  cellLayout->setContentsMargins(4, 2, 4, 2);
  cellLayout->setSpacing(0);
  cellLayout->addWidget(combo);
  table->setCellWidget(row, column, cellHost.release());
}

QTableWidgetItem* makeImportCheckItem() {
  auto* item = new QTableWidgetItem();
  item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
  item->setCheckState(Qt::Checked);
  item->setTextAlignment(Qt::AlignCenter);
  return item;
}

void applyWidgetBackground(QWidget* widget, const QColor& background) {
  if (!widget) return;
  widget->setAutoFillBackground(true);
  QPalette palette = widget->palette();
  palette.setColor(QPalette::Window, background);
  widget->setPalette(palette);
}

constexpr int kQuarterCount = 4;

int quarterIndexForMainEvent(const QString& mainEvent) {
  for (int quarterIndex = 0; quarterIndex < kQuarterCount; ++quarterIndex) {
    if (mainEvent == EventDefaults::quarterCode(quarterIndex)) return quarterIndex;
  }
  return -1;
}

/// Canonical "+"-form of a parsed team code so "home foul+" matches "HOME FOUL-".
QString canonicalPositiveTeamCode(const QString& abbrev, const QString& shortCode) {
  return QStringLiteral("%1 %2+").arg(abbrev, shortCode);
}

struct ClosedQuarterSpan {
  bool present = false;
  qint64 startMs = 0;
  qint64 endMs = 0;
};

QString periodLabelAtMarkMs(qint64 markMs,
                            const std::array<ClosedQuarterSpan, kQuarterCount>& closedQuarters,
                            qint64 gameStartAnchorMs) {
  int matchingQuarterIndex = -1;
  qint64 matchingStartMs = 0;
  qint64 matchingEndMs = 0;
  for (int quarterIndex = 0; quarterIndex < kQuarterCount; ++quarterIndex) {
    const ClosedQuarterSpan& closedQuarter = closedQuarters[quarterIndex];
    if (!closedQuarter.present) continue;
    if (markMs < closedQuarter.startMs || markMs > closedQuarter.endMs) continue;
    const bool isBetterMatch =
        matchingQuarterIndex < 0 || closedQuarter.startMs < matchingStartMs ||
        (closedQuarter.startMs == matchingStartMs && closedQuarter.endMs < matchingEndMs);
    if (!isBetterMatch) continue;
    matchingQuarterIndex = quarterIndex;
    matchingStartMs = closedQuarter.startMs;
    matchingEndMs = closedQuarter.endMs;
  }
  if (matchingQuarterIndex >= 0) return EventDefaults::quarterCode(matchingQuarterIndex);

  int closedPrefixCount = 0;
  while (closedPrefixCount < kQuarterCount && closedQuarters[closedPrefixCount].present) {
    ++closedPrefixCount;
  }

  if (closedPrefixCount == kQuarterCount) {
    const ClosedQuarterSpan& finalQuarter = closedQuarters[kQuarterCount - 1];
    if (finalQuarter.present && markMs >= finalQuarter.startMs) {
      return EventDefaults::quarterCode(kQuarterCount - 1);
    }
    return QString();
  }

  if (closedPrefixCount > 0) {
    const qint64 currentQuarterStartMs = closedQuarters[closedPrefixCount - 1].endMs;
    if (markMs >= currentQuarterStartMs) {
      return EventDefaults::quarterCode(closedPrefixCount);
    }
    return QString();
  }

  if (gameStartAnchorMs >= 0 && markMs >= gameStartAnchorMs) {
    return EventDefaults::quarterCode(0);
  }
  return QString();
}

void inferPeriods(QVector<TagSession::GameTag>& tags) {
  std::array<ClosedQuarterSpan, kQuarterCount> closedQuarters{};
  qint64 gameStartAnchorMs = -1;

  for (const TagSession::GameTag& tag : tags) {
    if (tag.mainEvent == QLatin1String(EventDefaults::TimeCodes::kStartAnchor)) {
      if (gameStartAnchorMs < 0) gameStartAnchorMs = tag.startMs;
      continue;
    }
    const int quarterIndex = quarterIndexForMainEvent(tag.mainEvent);
    if (quarterIndex < 0) continue;
    ClosedQuarterSpan& span = closedQuarters[quarterIndex];
    span.present = true;
    span.startMs = tag.startMs;
    span.endMs = tag.endMs;
  }

  for (TagSession::GameTag& tag : tags) {
    if (!tag.period.isEmpty()) continue;
    if (EventDefaults::isQuarterEvent(tag.mainEvent)) {
      tag.period = tag.mainEvent;
      continue;
    }
    if (EventDefaults::isTimeControlEvent(tag.mainEvent)) continue;
    tag.period = periodLabelAtMarkMs(tag.markMs, closedQuarters, gameStartAnchorMs);
  }
}

} // namespace

XmlEventMappingDialog::XmlEventMappingDialog(const QVector<XmlImporter::ParsedInstance>& instances,
                                             qint64 offsetMs,
                                             const TagSession* session,
                                             QWidget* parent)
    : QDialog(parent), instances_(instances), offsetMs_(offsetMs), session_(session) {
  if (session_) {
    sessionHomeAbbrev_ = session_->homeAbbrev().trimmed().toUpper();
    sessionAwayAbbrev_ = session_->awayAbbrev().trimmed().toUpper();
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
  auto layout = makeQtPtr<QVBoxLayout>(this);
  layout->setSpacing(10);

  auto titleLabel = makeQtPtr<QLabel>(this);
  Style::setRole(titleLabel.get(), "h2");
  titleLabel_ = titleLabel.get();
  layout->addWidget(titleLabel.get());

  auto instructionsLabel = makeQtPtr<QLabel>(this);
  instructionsLabel->setWordWrap(true);
  instructionsLabel_ = instructionsLabel.get();
  layout->addWidget(instructionsLabel.get());

  auto abbrevHeaderLabel = makeQtPtr<QLabel>(this);
  abbrevHeaderLabel_ = abbrevHeaderLabel.get();
  layout->addWidget(abbrevHeaderLabel.get());

  auto abbrevRow = makeQtPtr<QHBoxLayout>();
  abbrevRow->setSpacing(8);
  auto homeAbbrevLabel = makeQtPtr<QLabel>(this);
  auto xmlHomeAbbrevCombo = makeAbbrevComboWidget(this);
  auto awayAbbrevLabel = makeQtPtr<QLabel>(this);
  auto xmlAwayAbbrevCombo = makeAbbrevComboWidget(this);
  homeAbbrevLabel_ = homeAbbrevLabel.get();
  xmlHomeAbbrevCombo_ = xmlHomeAbbrevCombo.get();
  awayAbbrevLabel_ = awayAbbrevLabel.get();
  xmlAwayAbbrevCombo_ = xmlAwayAbbrevCombo.get();
  abbrevRow->addWidget(homeAbbrevLabel.get());
  abbrevRow->addWidget(xmlHomeAbbrevCombo.get());
  abbrevRow->addSpacing(20);
  abbrevRow->addWidget(awayAbbrevLabel.get());
  abbrevRow->addWidget(xmlAwayAbbrevCombo.get());
  abbrevRow->addStretch();
  layout->addLayout(abbrevRow.get());

  auto mappingTable = makeQtPtr<QTableWidget>(this);
  mappingTable->setObjectName(QStringLiteral("XmlEventMappingTable"));
  mappingTable->setColumnCount(5);
  mappingTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  mappingTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  mappingTable->setAlternatingRowColors(false);
  mappingTable_ = mappingTable.get();
  layout->addWidget(mappingTable.get(), 1);

  auto buttonRow = makeQtPtr<QHBoxLayout>();
  buttonRow->addStretch();
  auto cancelButton = makeQtPtr<QPushButton>(this);
  auto importButton = makeQtPtr<QPushButton>(this);
  Style::setVariant(importButton.get(), "primary");
  importButton->setDefault(true);
  cancelButton_ = cancelButton.get();
  importButton_ = importButton.get();
  buttonRow->addWidget(cancelButton.get());
  buttonRow->addWidget(importButton.get());
  layout->addLayout(buttonRow.get());

  connect(cancelButton.get(), &QPushButton::clicked, this, &QDialog::reject);
  connect(importButton.get(), &QPushButton::clicked, this, &XmlEventMappingDialog::onImportClicked);
  connect(xmlHomeAbbrevCombo.get(), &QComboBox::currentTextChanged, this,
          [this](const QString&) {
            onAbbrevMappingChanged();
            applyAutoMappings();
          });
  connect(xmlAwayAbbrevCombo.get(), &QComboBox::currentTextChanged, this,
          [this](const QString&) {
            onAbbrevMappingChanged();
            applyAutoMappings();
          });
  connect(mappingTable.get(), &QTableWidget::itemChanged, this,
          [this](QTableWidgetItem* item) {
            if (!item || item->column() != kColImport) return;
            updateRowImportState(item->row());
          });
}

void XmlEventMappingDialog::populateRows() {
  std::map<QString, int, CaseInsensitiveLess> codeCounts;
  QSet<QString> detectedAbbrevs;
  for (const XmlImporter::ParsedInstance& instance : instances_) {
    ++codeCounts[instance.code];
    const ParsedTeamCode parsed = parseTeamCodePattern(instance.code);
    if (parsed.valid && parsed.sign == QLatin1Char('+')) {
      detectedAbbrevs.insert(parsed.abbrev);
    }
  }

  QStringList abbrevChoices(detectedAbbrevs.cbegin(), detectedAbbrevs.cend());
  std::sort(abbrevChoices.begin(), abbrevChoices.end(),
            [](const QString& left, const QString& right) {
              return QString::compare(left, right, Qt::CaseInsensitive) < 0;
            });
  prependAbbrevIfAbsent(abbrevChoices, sessionHomeAbbrev_);
  prependAbbrevIfAbsent(abbrevChoices, sessionAwayAbbrev_);
  if (abbrevChoices.isEmpty()) {
    prependAbbrevIfAbsent(abbrevChoices, sessionHomeAbbrev_);
    prependAbbrevIfAbsent(abbrevChoices, sessionAwayAbbrev_);
  }

  xmlHomeAbbrevCombo_->addItems(abbrevChoices);
  xmlAwayAbbrevCombo_->addItems(abbrevChoices);
  if (!sessionHomeAbbrev_.isEmpty()) {
    const int homeIndex =
        xmlHomeAbbrevCombo_->findText(sessionHomeAbbrev_, Qt::MatchExactly);
    if (homeIndex >= 0) xmlHomeAbbrevCombo_->setCurrentIndex(homeIndex);
  }
  if (!sessionAwayAbbrev_.isEmpty()) {
    const int awayIndex =
        xmlAwayAbbrevCombo_->findText(sessionAwayAbbrev_, Qt::MatchExactly);
    if (awayIndex >= 0) xmlAwayAbbrevCombo_->setCurrentIndex(awayIndex);
  }

  mappingTable_->setRowCount(static_cast<int>(codeCounts.size()));
  rows_.clear();
  rows_.reserve(static_cast<int>(codeCounts.size()));

  const QStringList events = eventChoices();
  const QString homeLabel =
      teamSideDisplayLabel(QStringLiteral("Home"), sessionHomeAbbrev_, sessionAwayAbbrev_);
  const QString awayLabel =
      teamSideDisplayLabel(QStringLiteral("Away"), sessionHomeAbbrev_, sessionAwayAbbrev_);

  int row = 0;
  for (const auto& [code, count] : codeCounts) {
    MappingRow mappingRow;
    mappingRow.xmlCode = code;
    mappingRow.count = count;

    mappingTable_->setItem(row, kColCode, new QTableWidgetItem(code));
    mappingTable_->setItem(row, kColCount,
                           new QTableWidgetItem(QString::number(mappingRow.count)));

    auto eventCombo = makeTableComboWidget(mappingTable_);
    mappingRow.eventCombo = eventCombo.get();
    eventCombo->addItems(events);
    embedTableCombo(mappingTable_, row, kColEvent, eventCombo.get());

    auto teamCombo = makeTableComboWidget(mappingTable_);
    mappingRow.teamCombo = teamCombo.get();
    teamCombo->addItem(AppLocale::trUi("xml_import.mapping_team_none"), QString());
    teamCombo->addItem(homeLabel, QStringLiteral("Home"));
    teamCombo->addItem(awayLabel, QStringLiteral("Away"));
    embedTableCombo(mappingTable_, row, kColTeam, teamCombo.get());

    mappingRow.importItem = makeImportCheckItem();
    mappingTable_->setItem(row, kColImport, mappingRow.importItem);

    rows_.append(mappingRow);
    ++row;
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

  const MappingRow& mappingRow = rows_.at(row);
  if (mappingRow.eventCombo) {
    mappingRow.eventCombo->setEnabled(importing);
    applyWidgetBackground(mappingTable_->cellWidget(row, kColEvent), rowBackground);
  }
  if (mappingRow.teamCombo) {
    mappingRow.teamCombo->setEnabled(importing);
    applyWidgetBackground(mappingTable_->cellWidget(row, kColTeam), rowBackground);
  }
}

void XmlEventMappingDialog::refreshAllRowImportStates() {
  for (int row = 0; row < rows_.size(); ++row) {
    updateRowImportState(row);
  }
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
      positiveCodes.insert(canonicalPositiveTeamCode(parsed.abbrev, parsed.shortCode));
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
      setRowImportEnabled(i, true);
      continue;
    }

    const ParsedTeamCode parsed = parseTeamCodePattern(code);
    if (parsed.valid) {
      const std::optional<QString> mainEvent =
          EventCodeMap::mainEventForShortCode(parsed.shortCode);
      if (mainEvent.has_value()) {
        const int eventIndex = row.eventCombo->findText(*mainEvent);
        if (eventIndex >= 0) row.eventCombo->setCurrentIndex(eventIndex);
      }

      if (parsed.sign == QLatin1Char('-')) {
        const QString positiveCode =
            canonicalPositiveTeamCode(parsed.abbrev, parsed.shortCode);
        if (positiveCodes.contains(positiveCode)) {
          importEnabled = false;
          setRowImportEnabled(i, importEnabled);
          continue;
        }
      }

      if (parsed.sign == QLatin1Char('+')) {
        const QString team = teamForAbbrev(parsed.abbrev);
        if (!team.isEmpty()) {
          const int teamIndex = row.teamCombo->findData(team);
          if (teamIndex >= 0) row.teamCombo->setCurrentIndex(teamIndex);
          setRowImportEnabled(i, true);
          continue;
        }
      }
    }

    setRowImportEnabled(i, importEnabled);
  }

  refreshAllRowImportStates();
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

QHash<QString, XmlEventMappingDialog::CodeMapping> XmlEventMappingDialog::buildMappingByCode()
    const {
  QHash<QString, CodeMapping> mappingByCode;
  mappingByCode.reserve(rows_.size());
  for (const MappingRow& row : rows_) {
    mappingByCode.insert(row.xmlCode, mappingForRow(row));
  }
  return mappingByCode;
}

bool XmlEventMappingDialog::validateMappings(QString* errorMessage) const {
  const QHash<QString, CodeMapping> mappingByCode = buildMappingByCode();

  for (const XmlImporter::ParsedInstance& instance : instances_) {
    if (!mappingByCode.contains(instance.code)) {
      if (errorMessage) {
        *errorMessage = AppLocale::trUi("xml_import.mapping_missing_event").arg(instance.code);
      }
      return false;
    }
  }

  int importableCount = 0;
  for (const XmlImporter::ParsedInstance& instance : instances_) {
    const CodeMapping& mapping = mappingByCode[instance.code];
    if (!mapping.skip) ++importableCount;
  }

  if (importableCount == 0) {
    if (errorMessage) *errorMessage = AppLocale::trUi("xml_import.mapping_none_selected");
    return false;
  }

  for (const MappingRow& row : rows_) {
    const CodeMapping mapping = mappingByCode.value(row.xmlCode);
    if (mapping.skip) continue;

    if (row.count <= 0) {
      if (errorMessage) {
        *errorMessage = AppLocale::trUi("xml_import.mapping_missing_event").arg(row.xmlCode);
      }
      return false;
    }

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
    tag.markMs = tag.startMs;
  } else {
    const EventDefaults::EventDuration duration = EventDefaults::defaultFor(tag.mainEvent);
    tag.markMs = tag.startMs + duration.leadMs;
    if (tag.markMs > tag.endMs) tag.markMs = tag.endMs;
    if (tag.markMs < tag.startMs) tag.markMs = tag.startMs;
  }

  return tag;
}

XmlEventMappingDialog::ImportMappingResult XmlEventMappingDialog::buildImportSnapshot() const {
  ImportMappingResult result;
  const QHash<QString, CodeMapping> mappingByCode = buildMappingByCode();

  result.tags.reserve(instances_.size());
  for (const XmlImporter::ParsedInstance& instance : instances_) {
    const CodeMapping mapping = mappingByCode.value(instance.code);
    if (mapping.skip) {
      ++result.skippedInstanceCount;
      continue;
    }
    result.tags.append(gameTagFromInstance(instance, mapping));
  }

  inferPeriods(result.tags);
  return result;
}

void XmlEventMappingDialog::onImportClicked() {
  struct ImportButtonGuard {
    QPointer<QPushButton> button;
    ~ImportButtonGuard() {
      if (button) button->setEnabled(true);
    }
  } importButtonGuard{importButton_};
  if (importButton_) importButton_->setEnabled(false);

  QString errorMessage;
  if (!validateMappings(&errorMessage)) {
    QMessageBox::warning(this, AppLocale::trUi("xml_import.mapping_title"),
                         errorMessage.isEmpty() ? AppLocale::trUi("xml_import.mapping_none_selected")
                                                : errorMessage);
    return;
  }

  importResult_ = buildImportSnapshot();
  accept();
}

void XmlEventMappingDialog::applyUiStrings() {
  titleLabel_->setText(AppLocale::trUi("xml_import.mapping_title"));
  instructionsLabel_->setText(AppLocale::trUi("xml_import.mapping_instructions"));
  abbrevHeaderLabel_->setText(AppLocale::trUi("xml_import.mapping_abbrev_header"));
  if (homeAbbrevLabel_) {
    homeAbbrevLabel_->setText(
        abbrevMappingLabel(sessionHomeAbbrev_, "xml_import.mapping_home_abbrev"));
  }
  if (awayAbbrevLabel_) {
    awayAbbrevLabel_->setText(
        abbrevMappingLabel(sessionAwayAbbrev_, "xml_import.mapping_away_abbrev"));
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

  const QString homeLabel =
      teamSideDisplayLabel(QStringLiteral("Home"), sessionHomeAbbrev_, sessionAwayAbbrev_);
  const QString awayLabel =
      teamSideDisplayLabel(QStringLiteral("Away"), sessionHomeAbbrev_, sessionAwayAbbrev_);
  for (const MappingRow& row : rows_) {
    applyTeamComboLabels(row.teamCombo, homeLabel, awayLabel);
  }
}
