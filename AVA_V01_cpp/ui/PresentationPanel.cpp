#include "PresentationPanel.h"

#include "../i18n/AppLocale.h"
#include "../state/EventDefaults.h"
#include "../state/TagSession.h"
#include "../state/TeamAbbreviation.h"
#include "../state/TimeConvert.h"
#include "../style/StyleProps.h"
#include "../style/TabularDigits.h"
#include "../style/WorkDataView.h"
#include "QtPtr.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QStyleOptionSpinBox>
#include <QTableView>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr double kLeadLagStepSeconds = 0.5;
constexpr double kMinLeadLagSeconds = EventDefaults::kMinLeadLagMs / 1000.0;
constexpr double kMaxLeadLagSeconds = EventDefaults::kMaxLeadLagMs / 1000.0;
static_assert(EventDefaults::kMinLeadLagMs == static_cast<qint64>(kMinLeadLagSeconds * 1000.0));
static_assert(EventDefaults::kMaxLeadLagMs == static_cast<qint64>(kMaxLeadLagSeconds * 1000.0));

qint64 leadLagMillisecondsFromSpin(const QDoubleSpinBox* spinBox) {
  return TimeConvert::clampedMillisecondsFromSeconds(
      spinBox->value(), EventDefaults::kMinLeadLagMs, EventDefaults::kMaxLeadLagMs);
}

bool mouseHitsSpinBoxButton(const QDoubleSpinBox* spinBox, const QPoint& localPos) {
  if (!spinBox) return false;

  QStyleOptionSpinBox option;
  option.initFrom(spinBox);
  option.subControls = QStyle::SC_All;
  option.activeSubControls = QStyle::SC_None;
  option.buttonSymbols = spinBox->buttonSymbols();
  option.frame = spinBox->hasFrame();

  const QRect upRect = spinBox->style()->subControlRect(
      QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxUp, spinBox);
  const QRect downRect = spinBox->style()->subControlRect(
      QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxDown, spinBox);
  return upRect.contains(localPos) || downRect.contains(localPos);
}

/// Time column only. Draws the checkbox and row chrome with the style, then the
/// timestamp with fixed digit slots so proportional figures do not shift.
class TabularTimeDelegate final : public QStyledItemDelegate {
 public:
  using QStyledItemDelegate::QStyledItemDelegate;

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override {
    QStyleOptionViewItem styledOption = option;
    initStyleOption(&styledOption, index);
    const QString displayText = styledOption.text;
    styledOption.text.clear();

    const QWidget* widget = styledOption.widget;
    QStyle* style = widget ? widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &styledOption, painter, widget);

    if (displayText.isEmpty() || !painter) return;

    const QRect textRectangle =
        style->subElementRect(QStyle::SE_ItemViewItemText, &styledOption, widget);
    // QSS selection-color matches the normal text color. HighlightedText on
    // Fusion is white, which would disappear against the zinc selection fill.
    const QColor textColor = styledOption.palette.color(QPalette::Text);
    Style::drawTabularText(painter, textRectangle, displayText, styledOption.font, textColor);
  }

  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    QStyleOptionViewItem styledOption = option;
    initStyleOption(&styledOption, index);
    const QString displayText = styledOption.text;
    if (displayText.isEmpty()) return QStyledItemDelegate::sizeHint(option, index);

    // Ask the style for the widest digit in every slot. "09:59" and "10:00"
    // then share one size hint; subtracting the proportional advance left a
    // 1px residue from glyph side bearings.
    const QFontMetrics fontMetrics(styledOption.font);
    QChar widestDigit(u'0');
    int widestAdvance = -1;
    const QString digits = QStringLiteral("0123456789");
    for (const QChar digit : digits) {
      const int advance = fontMetrics.horizontalAdvance(digit);
      if (advance > widestAdvance) {
        widestAdvance = advance;
        widestDigit = digit;
      }
    }

    QString referenceText;
    referenceText.reserve(displayText.size());
    for (const QChar character : displayText) {
      referenceText.append(character.isDigit() ? widestDigit : character);
    }
    styledOption.text = referenceText;

    const QWidget* widget = styledOption.widget;
    QStyle* style = widget ? widget->style() : QApplication::style();
    return style->sizeFromContents(QStyle::CT_ItemViewItem, &styledOption, QSize(), widget);
  }
};

void releaseSpinBoxFocusSoon(QDoubleSpinBox* spinBox) {
  if (!spinBox) return;
  QTimer::singleShot(0, spinBox, [spinBoxGuard = QPointer<QDoubleSpinBox>(spinBox)]() {
    if (!spinBoxGuard) return;
    spinBoxGuard->clearFocus();
  });
}

struct FixtureTag {
  qint64 markMs = 0;
  QString teamKey;
  QString mainEvent;
  QString followUpEvent;
};

constexpr quint64 kFixtureTagIdBase = 0x7FFF0000ull;

bool presentationListBreakUiEnabled() {
  const QByteArray value = qgetenv("AVA_BREAK_UI");
  return value == "1" || value == "presenting" || value == "true";
}

QVector<FixtureTag> worstCaseFixtureTags() {
  // One failure per row, in the order a coach would scroll. Times stay small
  // except the long duration, which has to be a real timestamp to reach the
  // time column. Team keys other than Home/Away render as written.
  return {
      {0, QStringLiteral("Aleksandra Wiśniewska-Kowalczyk"), QStringLiteral("Circle Entry"),
       QString()},
      {1000, QStringLiteral("Jo"), QStringLiteral("Goal"), QString()},
      {2000, QString(), QStringLiteral("Shot"), QString()},
      {3000, QStringLiteral("Đặng Thị Ngọc Hân"), QStringLiteral("Benachrichtigungseinstellungen"),
       QString()},
      {4000, QStringLiteral("王秀英"), QStringLiteral("Circle Entry"),
       QStringLiteral("customer-feedback-from-enterprise-onboarding")},
      {5000, QStringLiteral("نور الهدى عبد الرحمن"), QStringLiteral("Goal"), QString()},
      {6000, QStringLiteral("🦊 Fox"), QStringLiteral("<script>alert(1)</script>"), QString()},
      {7000, QStringLiteral("  Sam   Lee "), QStringLiteral("Line one\nLine two"), QString()},
      {8000, QStringLiteral("Seán O'Brien-Ó Súilleabháin"), QStringLiteral("&amp;"),
       QStringLiteral("**bold**")},
      {9000, QStringLiteral("J"), QStringLiteral("J"), QString()},
      {10000, QStringLiteral("bartholomew.fitzgerald@northwind-industries-holdings.example.com"),
       QStringLiteral("Circle Entry"), QString()},
      {11000, QStringLiteral("Konstantin Oberhauser-Wettstein"),
       QStringLiteral("Senior Product Design Engineer, Platform Infrastructure"), QString()},
      {-5000, QStringLiteral("Jo"), QStringLiteral("Goal"), QString()},
      {1284LL * 3600LL * 1000LL, QStringLiteral("Jo"), QStringLiteral("Goal"), QString()},
  };
}

QVector<FixtureTag> oneFixtureTags() {
  return {{1000, QStringLiteral("Jo"), QStringLiteral("Goal"), QString()}};
}

QVector<FixtureTag> thousandFixtureTags() {
  QVector<FixtureTag> tags = worstCaseFixtureTags();
  tags.reserve(1000);
  for (int index = tags.size(); index < 1000; ++index) {
    tags.append(FixtureTag{static_cast<qint64>(index) * 1000, QStringLiteral("Jo"),
                           QStringLiteral("Circle Entry"), QString()});
  }
  return tags;
}

std::unique_ptr<QWidget, QtParentDeleter> makePresentationListDataToggle(QWidget* parent) {
  auto toggle = makeQtPtr<QWidget>(parent);
  toggle->setObjectName(QStringLiteral("PresentationListDataToggle"));
  toggle->setFocusPolicy(Qt::NoFocus);
  toggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  // Chrome for the stress test, not product UI. Kept off the theme roles so it
  // stays a plain segmented control and does not pick up button variants.
  toggle->setStyleSheet(QStringLiteral(
      "QWidget#PresentationListDataToggle {"
      "  background: #d4d4d8;"
      "  border-radius: 8px;"
      "}"
      "QWidget#PresentationListDataToggle QToolButton {"
      "  background: transparent;"
      "  border: none;"
      "  border-radius: 6px;"
      "  color: #3f3f46;"
      "  font-family: sans-serif;"
      "  font-size: 11px;"
      "  font-weight: 400;"
      "  padding: 2px 6px;"
      "  min-height: 18px;"
      "  max-height: 22px;"
      "  min-width: 0px;"
      "}"
      "QWidget#PresentationListDataToggle QToolButton:checked {"
      "  background: #ffffff;"
      "  color: #18181b;"
      "}"));

  // Two rows so every label fits the 260px side column without clipping.
  auto* layout = new QGridLayout(toggle.get());
  layout->setContentsMargins(3, 3, 3, 3);
  layout->setHorizontalSpacing(2);
  layout->setVerticalSpacing(2);
  layout->setColumnStretch(0, 1);
  layout->setColumnStretch(1, 1);
  layout->setColumnStretch(2, 1);

  auto group = makeQtPtr<QButtonGroup>(toggle.get());
  group->setExclusive(true);

  const struct Segment {
    const char* objectName;
    const char* label;
    int id;
  } segments[] = {
      {"PresentationListDataDemo", "Demo data", 0},
      {"PresentationListDataWorst", "Worst case", 1},
      {"PresentationListDataEmpty", "Empty", 2},
      {"PresentationListDataOne", "One", 3},
      {"PresentationListDataThousand", "1,000 rows", 4},
  };

  for (const Segment& segment : segments) {
    auto button = makeQtPtr<QToolButton>(toggle.get());
    button->setObjectName(QString::fromLatin1(segment.objectName));
    button->setText(QString::fromLatin1(segment.label));
    button->setCheckable(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    if (segment.id == 0) button->setChecked(true);
    group->addButton(button.get(), segment.id);
    const int row = segment.id < 3 ? 0 : 1;
    const int column = segment.id < 3 ? segment.id : segment.id - 3;
    layout->addWidget(button.get(), row, column);
  }

  return toggle;
}

QDoubleSpinBox* makeLeadLagSpinBox(QWidget* parent) {
  auto* spinBox = new QDoubleSpinBox(parent);
  spinBox->setRange(kMinLeadLagSeconds, kMaxLeadLagSeconds);
  spinBox->setSingleStep(kLeadLagStepSeconds);
  spinBox->setDecimals(1);
  spinBox->setSuffix(QStringLiteral(" s"));
  spinBox->setAlignment(Qt::AlignRight);
  spinBox->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
  spinBox->setFocusPolicy(Qt::ClickFocus);
  spinBox->setKeyboardTracking(true);
  return spinBox;
}

} // namespace

PresentationPanel::PresentationPanel(QWidget* parent)
    : QWidget(parent) {
  setObjectName(QStringLiteral("PresentationPanel"));
  buildUi();
  applyUiStrings();
  updateCurrentClipControlsEnabled();
  qApp->installEventFilter(this);
}

PresentationPanel::~PresentationPanel() {
  if (qApp) qApp->removeEventFilter(this);
  if (tagSession_) disconnect(tagSession_, nullptr, this, nullptr);
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

void PresentationPanel::buildUi() {
  auto* rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(0, 0, 0, 0);
  rootLayout->setSpacing(8);

  titleLabel_ = new QLabel(this);
  Style::setRole(titleLabel_, "h3");
  rootLayout->addWidget(titleLabel_);

  auto* filterRow = new QHBoxLayout();
  filterRow->setContentsMargins(0, 0, 0, 0);
  filterRow->setSpacing(8);

  eventFilterLabel_ = new QLabel(this);
  Style::setRole(eventFilterLabel_, "muted");
  eventFilterCombo_ = new QComboBox(this);
  eventFilterCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  connect(eventFilterCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &PresentationPanel::onFilterChanged);

  teamFilterLabel_ = new QLabel(this);
  Style::setRole(teamFilterLabel_, "muted");
  teamFilterCombo_ = new QComboBox(this);
  teamFilterCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  // Abbreviations are short. A stored code is not length-capped, so the closed
  // combo sizes to a few characters and elides instead of growing with the text.
  teamFilterCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  teamFilterCombo_->setMinimumContentsLength(4);
  if (QAbstractItemView* teamPopup = teamFilterCombo_->view()) {
    teamPopup->setMouseTracking(true);
  }
  connect(teamFilterCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &PresentationPanel::onFilterChanged);

  filterRow->addWidget(eventFilterLabel_);
  filterRow->addWidget(eventFilterCombo_, 1);
  filterRow->addWidget(teamFilterLabel_);
  filterRow->addWidget(teamFilterCombo_, 1);
  rootLayout->addLayout(filterRow);

  auto* selectionRow = new QHBoxLayout();
  selectionRow->setContentsMargins(0, 0, 0, 0);
  selectionRow->setSpacing(6);

  selectAllButton_ = new QToolButton(this);
  Style::setVariant(selectAllButton_, "ghost");
  Style::setSize(selectAllButton_, "sm");
  selectAllButton_->setCursor(Qt::PointingHandCursor);
  connect(selectAllButton_, &QToolButton::clicked, this, &PresentationPanel::onSelectAllClicked);

  selectNoneButton_ = new QToolButton(this);
  Style::setVariant(selectNoneButton_, "ghost");
  Style::setSize(selectNoneButton_, "sm");
  selectNoneButton_->setCursor(Qt::PointingHandCursor);
  connect(selectNoneButton_, &QToolButton::clicked, this, &PresentationPanel::onSelectNoneClicked);

  selectionSummaryLabel_ = new QLabel(this);
  Style::setRole(selectionSummaryLabel_, "muted");

  selectionRow->addWidget(selectAllButton_);
  selectionRow->addWidget(selectNoneButton_);
  selectionRow->addStretch(1);
  selectionRow->addWidget(selectionSummaryLabel_);
  rootLayout->addLayout(selectionRow);

  instancesModel_ = new PresentationInstancesModel(this);
  instancesTable_ = new QTableView(this);
  instancesTable_->setObjectName(QStringLiteral("PresentationInstancesTable"));
  instancesTable_->setModel(instancesModel_);
  Style::configureWorkDataView(instancesTable_);
  // Time stays as wide as the timestamp and checkbox. Team and event share the
  // remaining side-column width and elide, matching the stats tree in this pane.
  instancesTable_->horizontalHeader()->setStretchLastSection(false);
  instancesTable_->horizontalHeader()->setMinimumSectionSize(48);
  instancesTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  instancesTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  instancesTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  auto timeColumnDelegate = makeQtPtr<TabularTimeDelegate>(instancesTable_);
  instancesTable_->setItemDelegateForColumn(0, timeColumnDelegate.get());
  connect(instancesModel_, &PresentationInstancesModel::checkStateChanged, this,
          &PresentationPanel::onModelCheckStateChanged);
  connect(instancesTable_, &QTableView::doubleClicked, this,
          &PresentationPanel::onTableRowDoubleClicked);
  rootLayout->addWidget(instancesTable_, 1);

  auto* separator = new QFrame(this);
  separator->setFrameShape(QFrame::HLine);
  Style::setRole(separator, "separator");
  rootLayout->addWidget(separator);

  currentClipTitleLabel_ = new QLabel(this);
  Style::setRole(currentClipTitleLabel_, "h3");
  rootLayout->addWidget(currentClipTitleLabel_);

  auto* leadLagRow = new QHBoxLayout();
  leadLagRow->setContentsMargins(0, 0, 0, 0);
  leadLagRow->setSpacing(8);

  leadLabel_ = new QLabel(this);
  Style::setRole(leadLabel_, "muted");
  leadSpinBox_ = makeLeadLagSpinBox(this);
  leadSpinBox_->setObjectName(QStringLiteral("LeadSpinBox"));
  leadSpinBox_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  connect(leadSpinBox_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
          &PresentationPanel::onLeadLagSpinChanged);
  connect(leadSpinBox_, &QAbstractSpinBox::editingFinished, this, [this]() {
    if (leadSpinBox_ && leadSpinBox_->hasFocus()) leadSpinBox_->clearFocus();
  });

  lagLabel_ = new QLabel(this);
  Style::setRole(lagLabel_, "muted");
  lagSpinBox_ = makeLeadLagSpinBox(this);
  lagSpinBox_->setObjectName(QStringLiteral("LagSpinBox"));
  lagSpinBox_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  connect(lagSpinBox_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
          &PresentationPanel::onLeadLagSpinChanged);
  connect(lagSpinBox_, &QAbstractSpinBox::editingFinished, this, [this]() {
    if (lagSpinBox_ && lagSpinBox_->hasFocus()) lagSpinBox_->clearFocus();
  });

  leadLagRow->addWidget(leadLabel_);
  leadLagRow->addWidget(leadSpinBox_, 1);
  leadLagRow->addWidget(lagLabel_);
  leadLagRow->addWidget(lagSpinBox_, 1);
  rootLayout->addLayout(leadLagRow);

  applyToAllButton_ = new QPushButton(this);
  applyToAllButton_->setCursor(Qt::PointingHandCursor);
  Style::setVariant(applyToAllButton_, "outline");
  connect(applyToAllButton_, &QPushButton::clicked, this,
          &PresentationPanel::onApplyLeadLagToAllClicked);
  rootLayout->addWidget(applyToAllButton_);

  showNotesCheckBox_ = new QCheckBox(this);
  showNotesCheckBox_->setCursor(Qt::PointingHandCursor);
  showNotesCheckBox_->setChecked(true);
  connect(showNotesCheckBox_, &QCheckBox::toggled, this, &PresentationPanel::showNotesToggled);
  rootLayout->addWidget(showNotesCheckBox_);

  exportButton_ = new QPushButton(this);
  exportButton_->setCursor(Qt::PointingHandCursor);
  Style::setVariant(exportButton_, "primary");
  connect(exportButton_, &QPushButton::clicked, this, &PresentationPanel::exportRequested);
  rootLayout->addWidget(exportButton_);

  keyboardHintLabel_ = new QLabel(this);
  Style::setRole(keyboardHintLabel_, "faint");
  keyboardHintLabel_->setWordWrap(true);
  rootLayout->addWidget(keyboardHintLabel_);

  if (presentationListBreakUiEnabled()) {
    auto toggle = makePresentationListDataToggle(this);
    breakUiToggle_ = toggle.get();
    rootLayout->addWidget(toggle.get());
    if (auto* group = breakUiToggle_->findChild<QButtonGroup*>()) {
      connect(group, &QButtonGroup::idClicked, this, &PresentationPanel::onListFixtureChanged);
    }
  }
}

void PresentationPanel::applyUiStrings() {
  titleLabel_->setText(AppLocale::trUi("presentation.panel_title"));
  eventFilterLabel_->setText(AppLocale::trUi("presentation.filter_event"));
  teamFilterLabel_->setText(AppLocale::trUi("presentation.filter_team"));
  selectAllButton_->setText(AppLocale::trUi("presentation.select_all"));
  selectNoneButton_->setText(AppLocale::trUi("presentation.select_none"));
  currentClipTitleLabel_->setText(AppLocale::trUi("presentation.current_clip"));
  leadLabel_->setText(AppLocale::trUi("clip_durations.col_lead"));
  lagLabel_->setText(AppLocale::trUi("clip_durations.col_lag"));
  showNotesCheckBox_->setText(AppLocale::trUi("presentation.show_notes"));
  exportButton_->setText(AppLocale::trUi("presentation.export"));
  keyboardHintLabel_->setText(AppLocale::trUi("presentation.keyboard_hint"));
  instancesModel_->setColumnHeaders({AppLocale::trUi("tags.col_time"), AppLocale::trUi("tags.col_team"),
                                     AppLocale::trUi("tags.col_event")});

  refreshFromSession();
}

// ---------------------------------------------------------------------------
// Session data
// ---------------------------------------------------------------------------

void PresentationPanel::setTagSession(TagSession* session) {
  if (tagSession_ == session) return;
  if (tagSession_) disconnect(tagSession_, nullptr, this, nullptr);

  tagSession_ = session;
  selectedTagIdSet_.clear();
  currentTagId_ = 0;
  currentTagSessionIndex_ = -1;

  if (tagSession_) {
    connect(tagSession_, &TagSession::cleared, this, [this]() {
      selectedTagIdSet_.clear();
      currentTagId_ = 0;
      currentTagSessionIndex_ = -1;
      refreshFromSession();
    });
    connect(tagSession_, &TagSession::tagsImported, this, [this]() {
      selectedTagIdSet_.clear();
      currentTagId_ = 0;
      currentTagSessionIndex_ = -1;
      refreshFromSession();
    });
    connect(tagSession_, &TagSession::tagsChanged, this, &PresentationPanel::refreshFromSession);
    connect(tagSession_, &TagSession::gameMetadataChanged, this,
            &PresentationPanel::refreshFromSession);
    connect(tagSession_, &TagSession::tagNoteChanged, this, [this](int) { refreshFromSession(); });
  }

  refreshFromSession();
}

void PresentationPanel::refreshFromSession() {
  pruneSelectionToExistingTags();
  rebuildEventFilterOptions();
  rebuildRows();
  updateSelectionSummary();
  updateCurrentClipControlsEnabled();
  updateShowNotesCheckboxVisibility();

  const QVector<int> currentIndexes = selectedTagIndexes();
  if (currentIndexes != lastEmittedSelectedIndexes_) {
    lastEmittedSelectedIndexes_ = currentIndexes;
    emit selectedTagIndexesChanged(currentIndexes);
  }
}

bool PresentationPanel::eventFilterValueIsValid(const QString& eventFilter) const {
  if (eventFilter.isEmpty()) return true;
  if (EventDefaults::isTimeControlEvent(eventFilter)) return false;
  if (!tagSession_) return false;
  return tagSession_->mainEventCounts().value(eventFilter, 0) > 0;
}

void PresentationPanel::rebuildEventFilterOptions() {
  const QString previousEvent = eventFilterCombo_->currentData().toString();
  const QSignalBlocker eventBlocker(eventFilterCombo_);
  eventFilterCombo_->clear();
  eventFilterCombo_->addItem(AppLocale::trUi("presentation.all_events"), QString());

  if (tagSession_) {
    const auto& counts = tagSession_->mainEventCounts();
    QStringList mainEvents = counts.keys();
    mainEvents.sort(Qt::CaseInsensitive);
    for (const QString& mainEvent : mainEvents) {
      if (EventDefaults::isTimeControlEvent(mainEvent)) continue;
      const int count = counts.value(mainEvent, 0);
      if (count <= 0) continue;
      eventFilterCombo_->addItem(
          QStringLiteral("%1  (%2)").arg(AppLocale::trEvent(mainEvent)).arg(count), mainEvent);
    }
  }

  int restoredIndex = 0;
  if (eventFilterValueIsValid(previousEvent)) {
    const int foundIndex = eventFilterCombo_->findData(previousEvent);
    restoredIndex = foundIndex >= 0 ? foundIndex : 0;
  }
  eventFilterCombo_->setCurrentIndex(restoredIndex);

  const QString previousTeam = teamFilterCombo_->currentData().toString();
  const QSignalBlocker teamBlocker(teamFilterCombo_);
  teamFilterCombo_->clear();
  teamFilterCombo_->addItem(AppLocale::trUi("presentation.team_all"), QString());
  addTeamFilterItem(QStringLiteral("Home"));
  addTeamFilterItem(QStringLiteral("Away"));
  const int restoredTeamIndex = teamFilterCombo_->findData(previousTeam);
  teamFilterCombo_->setCurrentIndex(restoredTeamIndex >= 0 ? restoredTeamIndex : 0);
  updateTeamFilterTooltip();
}

PresentationInstancesModel::Row PresentationPanel::makeVisibleRow(int tagSessionIndex, quint64 tagId,
                                                                       qint64 markMs, const QString& teamKey,
                                                                       const QString& mainEvent,
                                                                       const QString& followUpEvent) const {
  const QString homeName = tagSession_ ? tagSession_->homeTeamName() : QString();
  const QString awayName = tagSession_ ? tagSession_->awayTeamName() : QString();
  const QString followUpForDisplay =
      AppLocale::followUpPathWithoutTeamSegments(followUpEvent, homeName, awayName);
  return {tagSessionIndex, tagId, markMs, teamDisplayName(teamKey),
          AppLocale::trDisplayTagLine(mainEvent, followUpForDisplay)};
}

QVector<PresentationInstancesModel::Row> PresentationPanel::buildFixtureRows() const {
  QVector<FixtureTag> tags;
  switch (listFixture_) {
  case ListFixture::WorstCase:
    tags = worstCaseFixtureTags();
    break;
  case ListFixture::Empty:
    break;
  case ListFixture::One:
    tags = oneFixtureTags();
    break;
  case ListFixture::Thousand:
    tags = thousandFixtureTags();
    break;
  case ListFixture::Session:
    break;
  }

  QVector<PresentationInstancesModel::Row> rows;
  rows.reserve(tags.size());
  for (int index = 0; index < tags.size(); ++index) {
    const FixtureTag& tag = tags.at(index);
    rows.append(makeVisibleRow(-1, kFixtureTagIdBase + static_cast<quint64>(index), tag.markMs,
                               tag.teamKey, tag.mainEvent, tag.followUpEvent));
  }
  return rows;
}

QVector<PresentationInstancesModel::Row> PresentationPanel::buildVisibleRows() const {
  if (listFixture_ != ListFixture::Session) return buildFixtureRows();

  QVector<PresentationInstancesModel::Row> visibleRows;
  if (!tagSession_) return visibleRows;

  const auto& tags = tagSession_->tags();
  visibleRows.reserve(tags.size());
  for (int tagIndex = 0; tagIndex < tags.size(); ++tagIndex) {
    const TagSession::GameTag& tag = tags.at(tagIndex);
    if (EventDefaults::isTimeControlEvent(tag.mainEvent)) continue;
    if (!passesFilters(tag.mainEvent, tag.team)) continue;

    visibleRows.append(makeVisibleRow(tagIndex, tag.id, tag.markMs, tag.team, tag.mainEvent,
                                      tag.followUpEvent));
  }

  std::sort(visibleRows.begin(), visibleRows.end(),
            [](const PresentationInstancesModel::Row& first,
               const PresentationInstancesModel::Row& second) {
              if (first.markMs != second.markMs) return first.markMs < second.markMs;
              return first.tagSessionIndex < second.tagSessionIndex;
            });
  return visibleRows;
}

void PresentationPanel::rebuildRows() {
  const int preservedScrollValue = instancesTable_->verticalScrollBar()->value();
  const quint64 previousCurrentTagId = currentTagId_;

  instancesModel_->setRows(buildVisibleRows());
  instancesModel_->setSelectedTagIds(selectedTagIdSet_);
  instancesModel_->setCurrentTagId(previousCurrentTagId);

  instancesTable_->resizeColumnToContents(0);
  instancesTable_->verticalScrollBar()->setValue(preservedScrollValue);

  const int currentRow = instancesModel_->rowForTagId(currentTagId_);
  if (currentRow >= 0) {
    instancesTable_->scrollTo(instancesModel_->index(currentRow, 0),
                              QAbstractItemView::EnsureVisible);
  }
}

void PresentationPanel::syncModelSelection() {
  instancesModel_->setSelectedTagIds(selectedTagIdSet_);
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

QVector<int> PresentationPanel::selectedTagIndexes() const {
  QVector<int> indexes;
  if (!tagSession_) return indexes;

  indexes.reserve(selectedTagIdSet_.size());
  for (quint64 tagId : selectedTagIdSet_) {
    const int tagIndex = tagSession_->indexOfTagId(tagId);
    if (tagIndex >= 0) indexes.append(tagIndex);
  }
  std::sort(indexes.begin(), indexes.end());
  return indexes;
}

void PresentationPanel::emitSelectionChanged() {
  lastEmittedSelectedIndexes_ = selectedTagIndexes();
  updateSelectionSummary();
  updateShowNotesCheckboxVisibility();
  emit selectedTagIndexesChanged(lastEmittedSelectedIndexes_);
}

void PresentationPanel::pruneSelectionToExistingTags() {
  if (!tagSession_) {
    selectedTagIdSet_.clear();
    currentTagId_ = 0;
    currentTagSessionIndex_ = -1;
    return;
  }

  QSet<quint64> existingTagIds;
  const auto& tags = tagSession_->tags();
  existingTagIds.reserve(tags.size());
  for (const TagSession::GameTag& tag : tags) {
    if (tag.id != 0) existingTagIds.insert(tag.id);
  }
  selectedTagIdSet_.intersect(existingTagIds);

  if (currentTagId_ != 0) {
    currentTagSessionIndex_ = tagSession_->indexOfTagId(currentTagId_);
    if (currentTagSessionIndex_ < 0) {
      currentTagId_ = 0;
      currentTagSessionIndex_ = -1;
    }
  } else {
    currentTagSessionIndex_ = -1;
  }

  if (currentTagSessionIndex_ >= 0) {
    const TagSession::GameTag& currentTag = tags.at(currentTagSessionIndex_);
    if (EventDefaults::isTimeControlEvent(currentTag.mainEvent)) {
      currentTagId_ = 0;
      currentTagSessionIndex_ = -1;
    }
  }
}

quint64 PresentationPanel::tagIdAt(int tagSessionIndex) const {
  if (!tagSession_ || !tagSession_->isValidTagIndex(tagSessionIndex)) return 0;
  return tagSession_->tags().at(tagSessionIndex).id;
}

bool PresentationPanel::resolveCurrentClip(int tagSessionIndex, int* resolvedIndex,
                                           quint64* resolvedTagId) const {
  if (!resolvedIndex || !resolvedTagId) return false;
  *resolvedIndex = -1;
  *resolvedTagId = 0;

  if (!tagSession_ || !tagSession_->isValidTagIndex(tagSessionIndex)) return false;

  const TagSession::GameTag& tag = tagSession_->tags().at(tagSessionIndex);
  if (tag.id == 0) return false;
  if (EventDefaults::isTimeControlEvent(tag.mainEvent)) return false;

  *resolvedIndex = tagSessionIndex;
  *resolvedTagId = tag.id;
  return true;
}

void PresentationPanel::onFilterChanged() {
  pruneSelectionToExistingTags();
  rebuildRows();
  updateSelectionSummary();
  updateCurrentClipControlsEnabled();
  updateTeamFilterTooltip();
}

void PresentationPanel::onModelCheckStateChanged(quint64 tagId, bool checked) {
  if (listFixture_ != ListFixture::Session) return;
  if (tagId == 0) return;
  if (checked) {
    selectedTagIdSet_.insert(tagId);
  } else {
    selectedTagIdSet_.remove(tagId);
  }
  emitSelectionChanged();
}

void PresentationPanel::onTableRowDoubleClicked(const QModelIndex& index) {
  if (!index.isValid() || !tagSession_) return;

  const int tagSessionIndex = instancesModel_->tagSessionIndexAt(index.row());
  const quint64 tagId = instancesModel_->tagIdAt(index.row());
  if (tagId == 0 || !tagSession_->isValidTagIndex(tagSessionIndex)) return;

  if (!selectedTagIdSet_.contains(tagId)) {
    selectedTagIdSet_.insert(tagId);
    syncModelSelection();
    emitSelectionChanged();
  }
  emit clipActivated(tagSessionIndex);
}

void PresentationPanel::onSelectAllClicked() {
  if (listFixture_ != ListFixture::Session) return;
  for (int row = 0; row < instancesModel_->rowCount(); ++row) {
    const quint64 tagId = instancesModel_->tagIdAt(row);
    if (tagId != 0) selectedTagIdSet_.insert(tagId);
  }
  syncModelSelection();
  emitSelectionChanged();
}

void PresentationPanel::onSelectNoneClicked() {
  if (listFixture_ != ListFixture::Session) return;
  // Only clears the instances currently listed, so filtered-out picks are preserved.
  for (int row = 0; row < instancesModel_->rowCount(); ++row) {
    const quint64 tagId = instancesModel_->tagIdAt(row);
    if (tagId != 0) selectedTagIdSet_.remove(tagId);
  }
  syncModelSelection();
  emitSelectionChanged();
}

void PresentationPanel::updateSelectionSummary() {
  const int visibleSelectedCount = instancesModel_->visibleSelectedCount(selectedTagIdSet_);
  const int listedCount = instancesModel_->rowCount();
  const int totalSelectedCount = selectedTagIndexes().size();
  selectionSummaryLabel_->setText(
      AppLocale::trUi("presentation.selection_summary")
          .arg(visibleSelectedCount)
          .arg(listedCount)
          .arg(totalSelectedCount));
  updateApplyButtonLabel();
}

void PresentationPanel::updateApplyButtonLabel() {
  if (!applyToAllButton_) return;
  const bool exactlyOneSelectedClip = selectedTagIndexes().size() == 1;
  const char* labelKey = exactlyOneSelectedClip ? "presentation.apply_to_one"
                                                : "presentation.apply_to_all";
  const char* tooltipKey = exactlyOneSelectedClip ? "presentation.apply_to_one_tooltip"
                                                  : "presentation.apply_to_all_tooltip";
  applyToAllButton_->setText(AppLocale::trUi(labelKey));
  applyToAllButton_->setToolTip(AppLocale::trUi(tooltipKey));
}

// ---------------------------------------------------------------------------
// Current clip
// ---------------------------------------------------------------------------

void PresentationPanel::setCurrentClip(int tagSessionIndex, qint64 leadMs, qint64 lagMs) {
  int resolvedIndex = -1;
  quint64 resolvedTagId = 0;
  resolveCurrentClip(tagSessionIndex, &resolvedIndex, &resolvedTagId);

  const bool currentClipChanged = currentTagSessionIndex_ != resolvedIndex;
  currentTagSessionIndex_ = resolvedIndex;
  currentTagId_ = resolvedTagId;

  {
    const QSignalBlocker leadBlocker(leadSpinBox_);
    const QSignalBlocker lagBlocker(lagSpinBox_);
    leadSpinBox_->setValue(leadMs / 1000.0);
    lagSpinBox_->setValue(lagMs / 1000.0);
  }

  if (currentClipChanged) {
    instancesModel_->setCurrentTagId(resolvedTagId);
    const int currentRow = instancesModel_->rowForTagId(resolvedTagId);
    if (currentRow >= 0) {
      instancesTable_->scrollTo(instancesModel_->index(currentRow, 0),
                                QAbstractItemView::EnsureVisible);
    }
  }
  updateCurrentClipControlsEnabled();
}

void PresentationPanel::clearCurrentClip() {
  currentTagSessionIndex_ = -1;
  currentTagId_ = 0;
  instancesModel_->setCurrentTagId(0);
  updateCurrentClipControlsEnabled();
}

bool PresentationPanel::showNotesEnabled() const {
  if (!showNotesCheckBox_ || !showNotesCheckBox_->isVisible()) return false;
  return showNotesCheckBox_->isChecked();
}

bool PresentationPanel::selectionHasAnyNotes() const {
  if (!tagSession_) return false;

  for (quint64 tagId : selectedTagIdSet_) {
    const int tagIndex = tagSession_->indexOfTagId(tagId);
    if (tagIndex < 0) continue;
    if (!tagSession_->tagNote(tagIndex).trimmed().isEmpty()) return true;
  }
  return false;
}

void PresentationPanel::updateShowNotesCheckboxVisibility() {
  if (!showNotesCheckBox_) return;
  showNotesCheckBox_->setVisible(selectionHasAnyNotes());
}

void PresentationPanel::setExportEnabled(bool enabled) { exportButton_->setEnabled(enabled); }

void PresentationPanel::updateCurrentClipControlsEnabled() {
  const bool hasCurrentClip =
      tagSession_ && tagSession_->isValidTagIndex(currentTagSessionIndex_) && currentTagId_ != 0;
  leadSpinBox_->setEnabled(hasCurrentClip);
  lagSpinBox_->setEnabled(hasCurrentClip);
  applyToAllButton_->setEnabled(hasCurrentClip);
}

void PresentationPanel::onLeadLagSpinChanged() {
  if (currentTagSessionIndex_ < 0 || !tagSession_) return;
  if (!tagSession_->isValidTagIndex(currentTagSessionIndex_) || currentTagId_ == 0) return;
  emit currentClipLeadLagEdited(leadLagMillisecondsFromSpin(leadSpinBox_),
                                leadLagMillisecondsFromSpin(lagSpinBox_));
}

void PresentationPanel::onApplyLeadLagToAllClicked() {
  emit applyLeadLagToAllRequested(leadLagMillisecondsFromSpin(leadSpinBox_),
                                  leadLagMillisecondsFromSpin(lagSpinBox_));
}

void PresentationPanel::onListFixtureChanged(int fixtureId) {
  if (fixtureId < static_cast<int>(ListFixture::Session) ||
      fixtureId > static_cast<int>(ListFixture::Thousand)) {
    return;
  }
  const auto nextFixture = static_cast<ListFixture>(fixtureId);
  if (nextFixture == listFixture_) return;
  listFixture_ = nextFixture;
  rebuildRows();
  updateSelectionSummary();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

QDoubleSpinBox* PresentationPanel::focusedLeadLagSpinBox() const {
  QWidget* focusWidget = QApplication::focusWidget();
  if (!focusWidget) return nullptr;
  if (leadSpinBox_ && (focusWidget == leadSpinBox_ || leadSpinBox_->isAncestorOf(focusWidget))) {
    return leadSpinBox_;
  }
  if (lagSpinBox_ && (focusWidget == lagSpinBox_ || lagSpinBox_->isAncestorOf(focusWidget))) {
    return lagSpinBox_;
  }
  return nullptr;
}

bool PresentationPanel::isInsideLeadLagSpinBox(const QWidget* widget) const {
  if (!widget) return false;
  if (leadSpinBox_ && (widget == leadSpinBox_ || leadSpinBox_->isAncestorOf(widget))) return true;
  if (lagSpinBox_ && (widget == lagSpinBox_ || lagSpinBox_->isAncestorOf(widget))) return true;
  return false;
}

void PresentationPanel::releaseLeadLagSpinBoxFocus() {
  if (QDoubleSpinBox* focusedSpinBox = focusedLeadLagSpinBox()) {
    focusedSpinBox->clearFocus();
  }
}

bool PresentationPanel::eventFilter(QObject* watched, QEvent* event) {
  if (!event) return QWidget::eventFilter(watched, event);

  auto* targetWidget = qobject_cast<QWidget*>(watched);
  const QEvent::Type eventType = event->type();

  if (eventType == QEvent::MouseButtonPress) {
    if (focusedLeadLagSpinBox() && !isInsideLeadLagSpinBox(targetWidget)) {
      releaseLeadLagSpinBoxFocus();
    }
  } else if (eventType == QEvent::MouseButtonRelease) {
    auto* spinBox = qobject_cast<QDoubleSpinBox*>(watched);
    if ((spinBox == leadSpinBox_ || spinBox == lagSpinBox_) && spinBox) {
      auto* mouseEvent = static_cast<QMouseEvent*>(event);
      if (mouseHitsSpinBoxButton(spinBox, mouseEvent->pos())) {
        releaseSpinBoxFocusSoon(spinBox);
      }
    }
  }

  return QWidget::eventFilter(watched, event);
}

bool PresentationPanel::passesFilters(const QString& mainEvent, const QString& team) const {
  const QString eventFilter = eventFilterCombo_->currentData().toString();
  if (!eventFilter.isEmpty() && mainEvent != eventFilter) return false;

  const QString teamFilter = teamFilterCombo_->currentData().toString();
  if (!teamFilter.isEmpty() && team != teamFilter) return false;

  return true;
}

QString PresentationPanel::teamDisplayName(const QString& teamKey) const {
  if (teamKey == QStringLiteral("Home")) {
    const QString name = tagSession_ ? tagSession_->homeTeamName() : QString();
    return name.isEmpty() ? AppLocale::trUi("export.team_home_default") : name;
  }
  if (teamKey == QStringLiteral("Away")) {
    const QString name = tagSession_ ? tagSession_->awayTeamName() : QString();
    return name.isEmpty() ? AppLocale::trUi("export.team_away_default") : name;
  }
  return teamKey.isEmpty() ? QStringLiteral("—") : teamKey;
}

QString PresentationPanel::teamFilterAbbreviation(const QString& teamKey) const {
  QString storedAbbreviation;
  if (tagSession_) {
    if (teamKey == QStringLiteral("Home")) storedAbbreviation = tagSession_->homeAbbrev();
    if (teamKey == QStringLiteral("Away")) storedAbbreviation = tagSession_->awayAbbrev();
  }
  if (!storedAbbreviation.trimmed().isEmpty()) return storedAbbreviation;

  const bool hasStoredName =
      tagSession_ && ((teamKey == QStringLiteral("Home") &&
                       !tagSession_->homeTeamName().trimmed().isEmpty()) ||
                      (teamKey == QStringLiteral("Away") &&
                       !tagSession_->awayTeamName().trimmed().isEmpty()));
  if (hasStoredName) {
    const QString derived = TeamAbbreviation::fromTeamName(teamDisplayName(teamKey));
    if (!derived.isEmpty()) return derived;
  }
  return teamDisplayName(teamKey);
}

void PresentationPanel::addTeamFilterItem(const QString& teamKey) {
  const QString fullName = teamDisplayName(teamKey);
  teamFilterCombo_->addItem(teamFilterAbbreviation(teamKey), teamKey);
  teamFilterCombo_->setItemData(teamFilterCombo_->count() - 1, fullName.toHtmlEscaped(),
                                Qt::ToolTipRole);
}

void PresentationPanel::updateTeamFilterTooltip() {
  if (!teamFilterCombo_) return;
  const QString teamKey = teamFilterCombo_->currentData().toString();
  if (teamKey.isEmpty()) {
    teamFilterCombo_->setToolTip(QString());
    return;
  }
  teamFilterCombo_->setToolTip(teamDisplayName(teamKey).toHtmlEscaped());
}
