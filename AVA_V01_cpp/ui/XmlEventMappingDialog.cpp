#include "XmlEventMappingDialog.h"

#include "../export/XmlImportTimeline.h"
#include "QtPtr.h"

#include "../i18n/AppLocale.h"
#include "../state/EventCodeMap.h"
#include "../state/EventDefaults.h"
#include "../style/StyleProps.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCollator>
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
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <memory>
#include <optional>

namespace {

constexpr int kColCode = 0;
constexpr int kColTeam = 1;
constexpr int kColEvent = 2;
constexpr int kColCount = 3;
constexpr int kColImport = 4;

class WheelIgnoringComboBox final : public QComboBox {
public:
  explicit WheelIgnoringComboBox(QWidget* parent = nullptr) : QComboBox(parent) {
    setFocusPolicy(Qt::StrongFocus);
  }

protected:
  void wheelEvent(QWheelEvent* event) override {
    if (!hasFocus()) {
      event->ignore();
      return;
    }
    QComboBox::wheelEvent(event);
  }
};

const QColor kActiveRowBgEven(0xFFFFFF);
const QColor kActiveRowBgOdd(0xFAFAFA);
const QColor kInactiveRowBg(0xF4F4F5);
const QColor kActiveText(0x09090B);
const QColor kInactiveText(0xA1A1AA);

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

std::unique_ptr<WheelIgnoringComboBox, QtParentDeleter> makeAbbrevComboWidget(QWidget* parent) {
  auto combo = makeQtPtr<WheelIgnoringComboBox>(parent);
  Style::setVariant(combo.get(), "abbrev");
  combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  combo->setMinimumWidth(56);
  return combo;
}

std::unique_ptr<WheelIgnoringComboBox, QtParentDeleter> makeTableComboWidget(QWidget* parent) {
  auto combo = makeQtPtr<WheelIgnoringComboBox>(parent);
  Style::setVariant(combo.get(), "compact");
  combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
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

/// Identity key for an imported XML code. Case and surrounding whitespace are not distinct.
QString normalizedMappingCode(const QString& code) {
  return code.trimmed().toUpper();
}

std::optional<XmlEventMappingDialog::CodeMapping> mappingForCode(
    const QHash<QString, XmlEventMappingDialog::CodeMapping>& mappingByCode,
    const QString& code) {
  const auto iterator = mappingByCode.constFind(normalizedMappingCode(code));
  if (iterator == mappingByCode.cend()) return std::nullopt;
  return iterator.value();
}

QString selectedXmlAbbrev(const QComboBox* combo) {
  if (!combo) return {};
  return combo->currentText().trimmed().toUpper();
}

bool xmlAbbrevsConflict(const QString& homeAbbrev, const QString& awayAbbrev) {
  return !homeAbbrev.isEmpty() && homeAbbrev == awayAbbrev;
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
    if (tag.mainEventId == EventCatalog::EventId::None &&
        tag.unlistedName == QLatin1String(EventDefaults::TimeCodes::kStartAnchor)) {
      if (gameStartAnchorMs < 0) gameStartAnchorMs = tag.startMs;
      continue;
    }
    const int quarterIndex = tag.mainEventId == EventCatalog::EventId::None
                                 ? quarterIndexForMainEvent(tag.unlistedName)
                                 : -1;
    if (quarterIndex < 0) continue;
    ClosedQuarterSpan& span = closedQuarters[quarterIndex];
    span.present = true;
    span.startMs = tag.startMs;
    span.endMs = tag.endMs;
  }

  for (TagSession::GameTag& tag : tags) {
    if (!tag.period.isEmpty()) continue;
    if (tag.mainEventId == EventCatalog::EventId::None &&
        EventDefaults::isQuarterEvent(tag.unlistedName)) {
      tag.period = tag.unlistedName;
      continue;
    }
    if (tag.mainEventKey().isTimeControl()) continue;
    tag.period = periodLabelAtMarkMs(tag.markMs, closedQuarters, gameStartAnchorMs);
  }
}

} // namespace

XmlEventMappingDialog::XmlEventMappingDialog(const QVector<XmlImporter::ParsedInstance>& instances,
                                             qint64 offsetMs,
                                             const TagSession* session,
                                             QWidget* parent)
    : QDialog(parent),
      sessionInstances_(instances),
      instances_(instances),
      offsetMs_(offsetMs),
      session_(session) {
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
  xmlHomeAbbrevCombo_->setObjectName(QStringLiteral("XmlHomeAbbrevCombo"));
  xmlAwayAbbrevCombo_->setObjectName(QStringLiteral("XmlAwayAbbrevCombo"));
  abbrevRow->addWidget(homeAbbrevLabel.get());
  abbrevRow->addWidget(xmlHomeAbbrevCombo.get());
  abbrevRow->addSpacing(20);
  abbrevRow->addWidget(awayAbbrevLabel.get());
  abbrevRow->addWidget(xmlAwayAbbrevCombo.get());
  abbrevRow->addStretch();
  layout->addLayout(abbrevRow.get());

  auto abbrevConflictLabel = makeQtPtr<QLabel>(this);
  abbrevConflictLabel->setObjectName(QStringLiteral("XmlAbbrevConflictLabel"));
  abbrevConflictLabel->setWordWrap(true);
  Style::setRole(abbrevConflictLabel.get(), "exportError");
  abbrevConflictLabel->setVisible(false);
  abbrevConflictLabel_ = abbrevConflictLabel.get();
  layout->addWidget(abbrevConflictLabel.get());

  auto importToolbar = makeQtPtr<QHBoxLayout>();
  importToolbar->addStretch();
  auto selectAllImportButton = makeQtPtr<QPushButton>(this);
  auto deselectAllImportButton = makeQtPtr<QPushButton>(this);
  Style::setSize(selectAllImportButton.get(), "sm");
  Style::setSize(deselectAllImportButton.get(), "sm");
  Style::setVariant(selectAllImportButton.get(), "ghost");
  Style::setVariant(deselectAllImportButton.get(), "ghost");
  selectAllImportButton->setObjectName(QStringLiteral("XmlMappingSelectAllImportButton"));
  deselectAllImportButton->setObjectName(QStringLiteral("XmlMappingDeselectAllImportButton"));
  selectAllImportButton_ = selectAllImportButton.get();
  deselectAllImportButton_ = deselectAllImportButton.get();
  importToolbar->addWidget(selectAllImportButton.get());
  importToolbar->addWidget(deselectAllImportButton.get());
  layout->addLayout(importToolbar.get());

  auto mappingTable = makeQtPtr<QTableWidget>(this);
  mappingTable->setObjectName(QStringLiteral("XmlEventMappingTable"));
  mappingTable->setColumnCount(5);
  mappingTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  mappingTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  mappingTable->setAlternatingRowColors(false);
  mappingTable->setSortingEnabled(false);
  mappingTable->horizontalHeader()->setSortIndicatorShown(true);
  mappingTable->horizontalHeader()->setSectionsClickable(true);
  mappingTable_ = mappingTable.get();
  layout->addWidget(mappingTable.get(), 1);

  if (mappingBreakUiEnabled()) {
    auto breakToggle = makeQtPtr<QWidget>(this);
    breakToggle->setObjectName(QStringLiteral("XmlMappingBreakUiToggle"));
    Style::setRole(breakToggle.get(), "breakUiToggle");
    auto* breakLayout = new QHBoxLayout(breakToggle.get());
    breakLayout->setContentsMargins(4, 4, 4, 4);
    breakLayout->setSpacing(2);
    auto breakGroup = makeQtPtr<QButtonGroup>(breakToggle.get());
    breakGroup->setExclusive(true);
    const struct BreakSegment {
      const char* objectName;
      const char* labelKey;
      MappingDataFixture fixture;
    } segments[] = {
        {"XmlMappingBreakUiDemo", "controls_editor.break_demo", MappingDataFixture::Session},
        {"XmlMappingBreakUiWorst", "controls_editor.break_worst", MappingDataFixture::WorstCase},
        {"XmlMappingBreakUiEmpty", "controls_editor.break_empty", MappingDataFixture::Empty},
        {"XmlMappingBreakUiOne", "controls_editor.break_one", MappingDataFixture::One},
    };
    for (const BreakSegment& segment : segments) {
      auto button = makeQtPtr<QToolButton>(breakToggle.get());
      button->setObjectName(QString::fromLatin1(segment.objectName));
      button->setText(AppLocale::trUi(segment.labelKey));
      button->setCheckable(true);
      button->setFocusPolicy(Qt::NoFocus);
      Style::setRole(button.get(), "breakUiSegment");
      if (segment.fixture == MappingDataFixture::Session) button->setChecked(true);
      breakGroup->addButton(button.get(), static_cast<int>(segment.fixture));
      breakLayout->addWidget(button.get());
      connect(button.get(), &QToolButton::clicked, this, [this, fixture = segment.fixture]() {
        applyMappingDataFixture(fixture);
      });
    }
    auto thousandButton = makeQtPtr<QToolButton>(breakToggle.get());
    thousandButton->setObjectName(QStringLiteral("XmlMappingBreakUiThousand"));
    thousandButton->setText(QStringLiteral("1,000 rows"));
    thousandButton->setCheckable(true);
    thousandButton->setFocusPolicy(Qt::NoFocus);
    Style::setRole(thousandButton.get(), "breakUiSegment");
    breakGroup->addButton(thousandButton.get(), static_cast<int>(MappingDataFixture::Thousand));
    breakLayout->addWidget(thousandButton.get());
    connect(thousandButton.get(), &QToolButton::clicked, this, [this]() {
      applyMappingDataFixture(MappingDataFixture::Thousand);
    });
    breakUiToggle_ = breakToggle.get();
    layout->addWidget(breakToggle.get());
  }

  auto buttonRow = makeQtPtr<QHBoxLayout>();
  buttonRow->addStretch();
  auto cancelButton = makeQtPtr<QPushButton>(this);
  auto importButton = makeQtPtr<QPushButton>(this);
  Style::setVariant(importButton.get(), "primary");
  importButton->setDefault(true);
  cancelButton_ = cancelButton.get();
  importButton_ = importButton.get();
  importButton_->setObjectName(QStringLiteral("XmlMappingImportButton"));
  buttonRow->addWidget(cancelButton.get());
  buttonRow->addWidget(importButton.get());
  layout->addLayout(buttonRow.get());

  connect(cancelButton.get(), &QPushButton::clicked, this, &QDialog::reject);
  connect(importButton.get(), &QPushButton::clicked, this, &XmlEventMappingDialog::onImportClicked);
  connect(selectAllImportButton.get(), &QPushButton::clicked, this,
          &XmlEventMappingDialog::onSelectAllImportClicked);
  connect(deselectAllImportButton.get(), &QPushButton::clicked, this,
          &XmlEventMappingDialog::onDeselectAllImportClicked);
  connect(mappingTable->horizontalHeader(), &QHeaderView::sectionClicked, this,
          &XmlEventMappingDialog::onMappingHeaderClicked);
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

void XmlEventMappingDialog::clearMappingTableUi() {
  if (mappingTable_) mappingTable_->setRowCount(0);
  rows_.clear();
  if (xmlHomeAbbrevCombo_) {
    QSignalBlocker blocker(xmlHomeAbbrevCombo_);
    xmlHomeAbbrevCombo_->clear();
  }
  if (xmlAwayAbbrevCombo_) {
    QSignalBlocker blocker(xmlAwayAbbrevCombo_);
    xmlAwayAbbrevCombo_->clear();
  }
}

void XmlEventMappingDialog::cacheRowWidgetState(int row) {
  if (row < 0 || row >= rows_.size()) return;
  MappingRow& mappingRow = rows_[row];
  if (mappingRow.eventCombo) mappingRow.cachedEvent = mappingRow.eventCombo->currentText();
  if (mappingRow.teamCombo) {
    mappingRow.cachedTeam = mappingRow.teamCombo->currentData().toString();
    mappingRow.cachedTeamLabel = mappingRow.teamCombo->currentText();
  }
  mappingRow.cachedImport = isRowImportEnabled(row);
}

void XmlEventMappingDialog::cacheAllRowWidgetState() {
  for (int row = 0; row < rows_.size(); ++row) {
    cacheRowWidgetState(row);
  }
}

void XmlEventMappingDialog::rebuildMappingTableFromRows() {
  if (!mappingTable_) return;

  const QStringList events = eventChoices();
  const QString homeLabel =
      teamSideDisplayLabel(QStringLiteral("Home"), sessionHomeAbbrev_, sessionAwayAbbrev_);
  const QString awayLabel =
      teamSideDisplayLabel(QStringLiteral("Away"), sessionHomeAbbrev_, sessionAwayAbbrev_);

  QSignalBlocker tableBlocker(mappingTable_);
  mappingTable_->setRowCount(rows_.size());

  for (int row = 0; row < rows_.size(); ++row) {
    MappingRow& mappingRow = rows_[row];

    auto* codeItem = new QTableWidgetItem(mappingRow.xmlCode);
    codeItem->setToolTip(mappingRow.xmlCode);
    mappingTable_->setItem(row, kColCode, codeItem);

    auto teamCombo = makeTableComboWidget(mappingTable_);
    mappingRow.teamCombo = teamCombo.get();
    teamCombo->addItem(AppLocale::trUi("xml_import.mapping_team_none"), QString());
    teamCombo->addItem(homeLabel, QStringLiteral("Home"));
    teamCombo->addItem(awayLabel, QStringLiteral("Away"));
    if (!mappingRow.cachedTeam.isEmpty()) {
      const int teamIndex = teamCombo->findData(mappingRow.cachedTeam);
      if (teamIndex >= 0) teamCombo->setCurrentIndex(teamIndex);
    }
    embedTableCombo(mappingTable_, row, kColTeam, teamCombo.get());

    auto eventCombo = makeTableComboWidget(mappingTable_);
    mappingRow.eventCombo = eventCombo.get();
    eventCombo->addItems(events);
    if (!mappingRow.cachedEvent.isEmpty()) {
      const int eventIndex = eventCombo->findText(mappingRow.cachedEvent);
      if (eventIndex >= 0) eventCombo->setCurrentIndex(eventIndex);
    }
    embedTableCombo(mappingTable_, row, kColEvent, eventCombo.get());

    auto* countItem = new QTableWidgetItem(QString::number(mappingRow.count));
    countItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    countItem->setData(Qt::UserRole, mappingRow.count);
    mappingTable_->setItem(row, kColCount, countItem);

    mappingRow.importItem = makeImportCheckItem();
    mappingRow.importItem->setCheckState(mappingRow.cachedImport ? Qt::Checked : Qt::Unchecked);
    mappingTable_->setItem(row, kColImport, mappingRow.importItem);
  }

  configureMappingTable();
  refreshAllRowImportStates();
}

void XmlEventMappingDialog::populateRows() {
  struct CodeGroup {
    QString displayCode;
    int count = 0;
  };

  QVector<CodeGroup> groups;
  QHash<QString, int> groupIndexByCode;
  QSet<QString> detectedAbbrevs;
  for (const XmlImporter::ParsedInstance& instance : instances_) {
    const QString lookupKey = normalizedMappingCode(instance.code);
    const auto existingGroup = groupIndexByCode.constFind(lookupKey);
    if (existingGroup == groupIndexByCode.cend()) {
      groupIndexByCode.insert(lookupKey, groups.size());
      CodeGroup group;
      group.displayCode = instance.code.trimmed();
      group.count = 1;
      groups.append(group);
    } else {
      groups[existingGroup.value()].count += 1;
    }

    const ParsedTeamCode parsed = parseTeamCodePattern(instance.code);
    if (parsed.valid && parsed.sign == QLatin1Char('+')) {
      detectedAbbrevs.insert(parsed.abbrev);
    }
  }

  std::sort(groups.begin(), groups.end(), [](const CodeGroup& left, const CodeGroup& right) {
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    return collator.compare(left.displayCode, right.displayCode) < 0;
  });

  QStringList abbrevChoices(detectedAbbrevs.cbegin(), detectedAbbrevs.cend());
  std::sort(abbrevChoices.begin(), abbrevChoices.end(), [](const QString& left, const QString& right) {
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    return collator.compare(left, right) < 0;
  });
  prependAbbrevIfAbsent(abbrevChoices, sessionHomeAbbrev_);
  prependAbbrevIfAbsent(abbrevChoices, sessionAwayAbbrev_);
  if (abbrevChoices.isEmpty()) {
    prependAbbrevIfAbsent(abbrevChoices, sessionHomeAbbrev_);
    prependAbbrevIfAbsent(abbrevChoices, sessionAwayAbbrev_);
  }

  clearMappingTableUi();

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

  rows_.reserve(groups.size());
  for (const CodeGroup& group : groups) {
    MappingRow mappingRow;
    mappingRow.xmlCode = group.displayCode;
    mappingRow.count = group.count;
    mappingRow.cachedImport = true;
    rows_.append(mappingRow);
  }

  rebuildMappingTableFromRows();
  if (mappingTable_ && mappingTable_->horizontalHeader()) {
    mappingTable_->horizontalHeader()->setSortIndicator(sortColumn_, sortOrder_);
  }
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
  header->setSectionResizeMode(kColTeam, QHeaderView::ResizeToContents);
  header->setSectionResizeMode(kColEvent, QHeaderView::Stretch);
  header->setSectionResizeMode(kColCount, QHeaderView::ResizeToContents);
  header->setSectionResizeMode(kColImport, QHeaderView::Fixed);
  mappingTable_->setColumnWidth(kColImport, 56);
  header->setMinimumSectionSize(72);
}

void XmlEventMappingDialog::sortMappingRows(int column, Qt::SortOrder order) {
  if (!mappingTable_ || rows_.isEmpty()) return;
  if (column < 0 || column >= mappingTable_->columnCount()) return;

  cacheAllRowWidgetState();

  QCollator collator;
  collator.setCaseSensitivity(Qt::CaseInsensitive);

  const auto compareRows = [&](const MappingRow& left, const MappingRow& right) {
    int comparison = 0;
    switch (column) {
    case kColCode:
      comparison = collator.compare(left.xmlCode, right.xmlCode);
      break;
    case kColTeam:
      comparison = collator.compare(left.cachedTeamLabel, right.cachedTeamLabel);
      break;
    case kColEvent:
      comparison = collator.compare(left.cachedEvent, right.cachedEvent);
      break;
    case kColCount:
      if (left.count < right.count) comparison = -1;
      else if (left.count > right.count) comparison = 1;
      break;
    case kColImport:
      if (left.cachedImport != right.cachedImport) {
        comparison = left.cachedImport ? -1 : 1;
      }
      break;
    default:
      break;
    }
    if (order == Qt::DescendingOrder) comparison = -comparison;
    return comparison < 0;
  };

  std::stable_sort(rows_.begin(), rows_.end(), compareRows);

  rebuildMappingTableFromRows();
  if (QHeaderView* header = mappingTable_->horizontalHeader()) {
    header->setSortIndicator(column, order);
  }
}

void XmlEventMappingDialog::onMappingHeaderClicked(int logicalIndex) {
  if (logicalIndex == sortColumn_) {
    sortOrder_ =
        sortOrder_ == Qt::AscendingOrder ? Qt::DescendingOrder : Qt::AscendingOrder;
  } else {
    sortColumn_ = logicalIndex;
    sortOrder_ = Qt::AscendingOrder;
  }
  sortMappingRows(sortColumn_, sortOrder_);
}

void XmlEventMappingDialog::onSelectAllImportClicked() {
  QSignalBlocker blocker(mappingTable_);
  for (int row = 0; row < rows_.size(); ++row) {
    setRowImportEnabled(row, true);
  }
  refreshAllRowImportStates();
}

void XmlEventMappingDialog::onDeselectAllImportClicked() {
  QSignalBlocker blocker(mappingTable_);
  for (int row = 0; row < rows_.size(); ++row) {
    setRowImportEnabled(row, false);
  }
  refreshAllRowImportStates();
}

bool XmlEventMappingDialog::mappingBreakUiEnabled() const {
  const QByteArray value = qgetenv("AVA_BREAK_UI");
  return value == "1" || value == QByteArrayLiteral("true") ||
         value == QByteArrayLiteral("xml_mapping");
}

QVector<XmlImporter::ParsedInstance> XmlEventMappingDialog::instancesForFixture(
    MappingDataFixture fixture) const {
  if (fixture == MappingDataFixture::Session) return sessionInstances_;
  if (fixture == MappingDataFixture::Empty) return {};

  auto make = [](const QString& code, qint64 startMs) {
    XmlImporter::ParsedInstance instance;
    instance.code = code;
    instance.startMs = startMs;
    instance.endMs = startMs + 1000;
    return instance;
  };

  if (fixture == MappingDataFixture::One) {
    return {make(QStringLiteral("HOME GOAL+"), 0)};
  }

  if (fixture == MappingDataFixture::WorstCase) {
    return {
        make(QStringLiteral("NORTHWIND PENALTY_CORNER_SEQUENCE_FINAL_REVISION_12+"), 0),
        make(QStringLiteral("Seán O'Brien-Müller 16YD+"), 1000),
        make(QStringLiteral("ĐẶNG THỊ NGỌC HÂN SHOT+"), 2000),
        make(QStringLiteral("🦊 FOUL+"), 3000),
        make(QStringLiteral("AWAY GOAL+"), 4000),
        make(QStringLiteral("HOME GOAL-"), 5000),
        make(QStringLiteral("Q1"), 6000),
        make(QStringLiteral("HOME GOAL+"), 7000),
    };
  }

  QVector<XmlImporter::ParsedInstance> instances;
  instances.reserve(1000);
  for (int index = 0; index < 1000; ++index) {
    instances.append(make(QStringLiteral("HOME SHOT+"), static_cast<qint64>(index) * 1000));
  }
  return instances;
}

void XmlEventMappingDialog::applyMappingDataFixture(MappingDataFixture fixture) {
  instances_ = instancesForFixture(fixture);
  sortColumn_ = kColCode;
  sortOrder_ = Qt::AscendingOrder;
  populateRows();
  applyAutoMappings();
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

bool XmlEventMappingDialog::abbreviationsCurrentlyConflict() const {
  return xmlAbbrevsConflict(selectedXmlAbbrev(xmlHomeAbbrevCombo_),
                            selectedXmlAbbrev(xmlAwayAbbrevCombo_));
}

void XmlEventMappingDialog::updateAbbrevConflictState() {
  const bool conflict = abbreviationsCurrentlyConflict();
  if (abbrevConflictLabel_) {
    abbrevConflictLabel_->setText(conflict ? AppLocale::trUi("xml_import.mapping_same_abbrev")
                                           : QString());
    abbrevConflictLabel_->setVisible(conflict);
  }
  if (importButton_) importButton_->setEnabled(!conflict);
}

void XmlEventMappingDialog::onAbbrevMappingChanged() {
  xmlAbbrevToTeamSide_.clear();
  const QString homeAbbrev = selectedXmlAbbrev(xmlHomeAbbrevCombo_);
  const QString awayAbbrev = selectedXmlAbbrev(xmlAwayAbbrevCombo_);
  if (!xmlAbbrevsConflict(homeAbbrev, awayAbbrev)) {
    if (!homeAbbrev.isEmpty()) xmlAbbrevToTeamSide_.insert(homeAbbrev, QStringLiteral("Home"));
    if (!awayAbbrev.isEmpty()) xmlAbbrevToTeamSide_.insert(awayAbbrev, QStringLiteral("Away"));
  }
  updateAbbrevConflictState();
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
    mappingByCode.insert(normalizedMappingCode(row.xmlCode), mappingForRow(row));
  }
  return mappingByCode;
}

bool XmlEventMappingDialog::validateMappings(QString* errorMessage) const {
  if (abbreviationsCurrentlyConflict()) {
    if (errorMessage) *errorMessage = AppLocale::trUi("xml_import.mapping_same_abbrev");
    return false;
  }

  const QHash<QString, CodeMapping> mappingByCode = buildMappingByCode();

  for (const XmlImporter::ParsedInstance& instance : instances_) {
    if (!mappingForCode(mappingByCode, instance.code).has_value()) {
      if (errorMessage) {
        *errorMessage = AppLocale::trUi("xml_import.mapping_missing_event").arg(instance.code);
      }
      return false;
    }
  }

  int importableCount = 0;
  for (const XmlImporter::ParsedInstance& instance : instances_) {
    const std::optional<CodeMapping> mapping = mappingForCode(mappingByCode, instance.code);
    if (mapping.has_value() && !mapping->skip) ++importableCount;
  }

  if (importableCount == 0) {
    if (errorMessage) *errorMessage = AppLocale::trUi("xml_import.mapping_none_selected");
    return false;
  }

  for (const MappingRow& row : rows_) {
    const std::optional<CodeMapping> mapping = mappingForCode(mappingByCode, row.xmlCode);
    if (!mapping.has_value()) {
      if (errorMessage) {
        *errorMessage = AppLocale::trUi("xml_import.mapping_missing_event").arg(row.xmlCode);
      }
      return false;
    }
    if (mapping->skip) continue;

    if (row.count <= 0) {
      if (errorMessage) {
        *errorMessage = AppLocale::trUi("xml_import.mapping_missing_event").arg(row.xmlCode);
      }
      return false;
    }

    if (mapping->canonicalMainEvent.isEmpty()) {
      if (errorMessage) {
        *errorMessage = AppLocale::trUi("xml_import.mapping_missing_event").arg(row.xmlCode);
      }
      return false;
    }
    if (!EventDefaults::isTimeControlEvent(mapping->canonicalMainEvent) && mapping->team.isEmpty()) {
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
  tag.setMainEventKey(TagEvent::keyFromCanonicalName(mapping.canonicalMainEvent));
  tag.team = mapping.team;
  tag.period = instance.periodLabel;
  tag.intervalManuallyEdited = true;

  const ImportedClipInterval interval =
      clipIntervalOnVideoTimeline(instance.startMs, instance.endMs, offsetMs_);
  tag.startMs = interval.startMs;
  tag.endMs = interval.endMs;

  if (tag.mainEventKey().isTimeControl()) {
    tag.markMs = tag.startMs;
  } else {
    const EventDefaults::EventDuration duration =
        EventDefaults::defaultFor(TagEvent::canonicalName(tag.mainEventKey()));
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
    const std::optional<CodeMapping> mapping = mappingForCode(mappingByCode, instance.code);
    if (!mapping.has_value() || mapping->skip) {
      ++result.skippedInstanceCount;
      continue;
    }
    const ImportedClipInterval interval =
        clipIntervalOnVideoTimeline(instance.startMs, instance.endMs, offsetMs_);
    if (interval.adjustedBeforeVideoStart) {
      ++result.adjustedBeforeStartCount;
    }
    result.tags.append(gameTagFromInstance(instance, *mapping));
  }

  inferPeriods(result.tags);
  return result;
}

void XmlEventMappingDialog::onImportClicked() {
  struct ImportButtonGuard {
    XmlEventMappingDialog* dialog = nullptr;
    ~ImportButtonGuard() {
      if (dialog) dialog->updateAbbrevConflictState();
    }
  } importButtonGuard{this};
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

  if (selectAllImportButton_) {
    selectAllImportButton_->setText(AppLocale::trUi("xml_import.mapping_select_all"));
  }
  if (deselectAllImportButton_) {
    deselectAllImportButton_->setText(AppLocale::trUi("xml_import.mapping_deselect_all"));
  }

  mappingTable_->setHorizontalHeaderLabels({
      AppLocale::trUi("xml_import.mapping_col_code"),
      AppLocale::trUi("xml_import.mapping_col_team"),
      AppLocale::trUi("xml_import.mapping_col_event"),
      AppLocale::trUi("xml_import.mapping_col_count"),
      AppLocale::trUi("xml_import.mapping_col_import"),
  });

  const QString homeLabel =
      teamSideDisplayLabel(QStringLiteral("Home"), sessionHomeAbbrev_, sessionAwayAbbrev_);
  const QString awayLabel =
      teamSideDisplayLabel(QStringLiteral("Away"), sessionHomeAbbrev_, sessionAwayAbbrev_);
  for (const MappingRow& row : rows_) {
    applyTeamComboLabels(row.teamCombo, homeLabel, awayLabel);
  }

  updateAbbrevConflictState();
}
