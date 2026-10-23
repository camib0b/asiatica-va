#include "GameControlsEditorDialog.h"

#include "GameControlsContextMenu.h"
#include "QtPtr.h"

#include "../i18n/AppLocale.h"
#include "../i18n/LocaleNotifier.h"
#include "../state/EventCatalog.h"
#include "../state/EventControlsSetup.h"
#include "../state/EventControlsTemplateStore.h"
#include "../state/EventControlsValidation.h"
#include "../state/EventLabelResolver.h"
#include "../state/TagSession.h"
#include "../style/StyleProps.h"
#include "../style/ThemeColors.h"

#include <QBrush>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSplitter>
#include <QStringList>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <QtGlobal>

#include <array>
#include <functional>
#include <utility>

namespace {

void addKeyboardRow(QVBoxLayout* column, QWidget* parent, const QList<QPushButton*>& buttons,
                    int leftOffsetUnits, int maximumLeftOffsetUnits) {
  if (!column) return;
  auto row = makeQtPtr<QWidget>(parent);
  row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  auto layout = makeQtPtr<QHBoxLayout>(row.get());
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(4);

  layout->addSpacerItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Minimum));
  layout->setStretch(layout->count() - 1, qMax(0, leftOffsetUnits));
  for (QPushButton* button : buttons) {
    if (!button) continue;
    layout->addWidget(button, EventCatalog::kButtonGridUnitsPerKey);
  }
  const int missingButtons = qMax(0, EventCatalog::kButtonGridColumnCount - buttons.size());
  const int rightOffsetUnits = qMax(0, maximumLeftOffsetUnits - leftOffsetUnits) +
                                (EventCatalog::kButtonGridUnitsPerKey * missingButtons);
  layout->addSpacerItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Minimum));
  layout->setStretch(layout->count() - 1, rightOffsetUnits);
  column->addWidget(row.get());
}

void fitTitleLabel(QLabel* titleLabel) {
  if (!titleLabel) return;
  const QString fullText = titleLabel->property("fullLabel").toString();
  titleLabel->setTextFormat(Qt::PlainText);
  const int availableWidth = titleLabel->contentsRect().width();
  const int availableHeight = titleLabel->contentsRect().height();
  QString shown = fullText;
  QString tip;
  if (availableWidth > 8 && availableHeight > 0) {
    const QFontMetrics metrics = titleLabel->fontMetrics();
    const QRect wrapped = metrics.boundingRect(QRect(0, 0, availableWidth, 1000),
                                               Qt::AlignHCenter | Qt::TextWordWrap, fullText);
    const bool fitsWrapped =
        wrapped.width() <= availableWidth + 1 && wrapped.height() <= availableHeight + 1;
    if (!fitsWrapped) {
      shown = metrics.elidedText(fullText, Qt::ElideRight, qMax(1, availableWidth));
      tip = fullText;
    }
  }
  if (titleLabel->text() != shown) titleLabel->setText(shown);
  if (titleLabel->toolTip() != tip) titleLabel->setToolTip(tip);
  if (QWidget* parent = titleLabel->parentWidget()) {
    if (parent->toolTip() != tip) parent->setToolTip(tip);
  }
}

QString displayLabel(EventCatalog::EventId eventId, const EventLabelOverrides::Table& table,
                     const QHash<int, QString>& pendingLabels) {
  const auto pending = pendingLabels.constFind(static_cast<int>(eventId));
  if (pending != pendingLabels.cend()) return pending.value();
  return EventLabelResolver::labelForId(eventId, table);
}

void accumulateFollowUps(const EventCatalog::Node& node, const EventLabelOverrides::Table& table,
                         int* total, int* shown) {
  for (int childIndex = 0; childIndex < node.childCount; ++childIndex) {
    const EventCatalog::EventId childId = node.children[static_cast<std::size_t>(childIndex)];
    const EventCatalog::Node* child = EventCatalog::nodeForId(childId);
    if (!child) continue;
    ++*total;
    if (!table.hidden(childId)) ++*shown;
    accumulateFollowUps(*child, table, total, shown);
  }
}

QString followUpCountText(int shown, int total) {
  if (total <= 0) return AppLocale::trUi("controls_editor.follow_ups_none");
  if (shown <= 0) return AppLocale::trUi("controls_editor.follow_ups_none_shown");
  if (shown == 1 && total == 1) return AppLocale::trUi("controls_editor.follow_ups_one");
  if (shown == 1) return AppLocale::trUi("controls_editor.follow_ups_one_shown").arg(total);
  if (shown == total) return AppLocale::trUi("controls_editor.follow_ups_many").arg(total);
  return AppLocale::trUi("controls_editor.follow_ups_many_shown").arg(shown).arg(total);
}

QString templateDisplayName(const EventControlsTemplateStore::ListedTemplate& listing) {
  if (listing.builtin) return AppLocale::trUi("controls_editor.builtin_name");
  return listing.name;
}

void appendIssueMessage(QStringList* messages, const EventControlsValidation::Issue& issue,
                        const EventLabelOverrides::Table& table) {
  if (!messages || issue.kind == EventControlsValidation::IssueKind::None) return;
  const QString text = EventControlsValidation::issueMessage(issue, table);
  if (!text.isEmpty() && !messages->contains(text)) messages->append(text);
}

}  // namespace

GameControlsEditorDialog::GameControlsEditorDialog(TagSession* session, QWidget* parent,
                                                   const QString& templateStorageRoot)
    : QDialog(parent),
      tagSession_(session),
      templateStorageRoot_(templateStorageRoot),
      selectedEventId_(EventCatalog::EventId::None),
      suppressDetailEdits_(false) {
  if (tagSession_) {
    draft_ = EventControlsSetup::fromSession(*tagSession_).labelOverrides;
  }
  setObjectName(QStringLiteral("ControlsEditorDialog"));
  Style::setRole(this, "controlsEditor");
  setModal(true);
  setSizeGripEnabled(true);

  buildUi();
  buildBreakToggle();
  reloadTemplateList(EventControlsSetup::builtinDefaultName());
  applyUiStrings();
  const EventCatalog::EventId firstEvent =
      EventCatalog::buttonGridCount() > 0 ? EventCatalog::buttonGridNodeAt(0).id
                                          : EventCatalog::EventId::None;
  selectEvent(firstEvent, true);

  connect(&LocaleNotifier::instance(), &LocaleNotifier::languageChanged, this,
          &GameControlsEditorDialog::applyUiStrings);
}

void GameControlsEditorDialog::buildUi() {
  auto rootLayout = makeQtPtr<QVBoxLayout>(this);
  rootLayout->setSpacing(12);
  rootLayout->setContentsMargins(24, 20, 24, 20);

  auto title = makeQtPtr<QLabel>(this);
  title->setObjectName(QStringLiteral("ControlsEditorTitle"));
  title->setTextFormat(Qt::PlainText);
  title->setWordWrap(true);
  Style::setRole(title.get(), "h2");
  titleLabel_ = title.get();
  rootLayout->addWidget(title.get());

  auto subtitle = makeQtPtr<QLabel>(this);
  subtitle->setObjectName(QStringLiteral("ControlsEditorSubtitle"));
  subtitle->setTextFormat(Qt::PlainText);
  subtitle->setWordWrap(true);
  Style::setRole(subtitle.get(), "muted");
  subtitleLabel_ = subtitle.get();
  rootLayout->addWidget(subtitle.get());

  buildTemplateBar(rootLayout.get());
  buildEditorBody(rootLayout.get());

  auto banner = makeQtPtr<QLabel>(this);
  banner->setObjectName(QStringLiteral("ControlsEditorBanner"));
  banner->setWordWrap(true);
  banner->setTextFormat(Qt::PlainText);
  banner->setMinimumWidth(0);
  Style::setRole(banner.get(), "inlineError");
  banner_ = banner.get();
  rootLayout->addWidget(banner.get());

  auto buttonRow = makeQtPtr<QHBoxLayout>();
  buttonRow->setSpacing(8);
  auto resetButton = makeQtPtr<QPushButton>(this);
  resetButton->setObjectName(QStringLiteral("ControlsEditorReset"));
  Style::setVariant(resetButton.get(), "ghost");
  Style::setSize(resetButton.get(), "sm");
  resetButton_ = resetButton.get();
  buttonRow->addWidget(resetButton.get());
  buttonRow->addStretch(1);
  auto cancelButton = makeQtPtr<QPushButton>(this);
  cancelButton->setObjectName(QStringLiteral("ControlsEditorCancel"));
  Style::setVariant(cancelButton.get(), "ghost");
  Style::setSize(cancelButton.get(), "sm");
  cancelButton_ = cancelButton.get();
  buttonRow->addWidget(cancelButton.get());
  auto saveButton = makeQtPtr<QPushButton>(this);
  saveButton->setObjectName(QStringLiteral("ControlsEditorSave"));
  Style::setVariant(saveButton.get(), "primary");
  Style::setSize(saveButton.get(), "sm");
  saveButton_ = saveButton.get();
  buttonRow->addWidget(saveButton.get());
  rootLayout->addLayout(buttonRow.get());

  connect(resetButton_, &QPushButton::clicked, this, [this]() { onResetClicked(); });
  connect(cancelButton_, &QPushButton::clicked, this, &QDialog::reject);
  connect(saveButton_, &QPushButton::clicked, this, [this]() { onSaveClicked(); });
}

void GameControlsEditorDialog::buildTemplateBar(QLayout* rootLayout) {
  auto* columnLayout = qobject_cast<QVBoxLayout*>(rootLayout);
  if (!columnLayout) return;

  auto templateRow = makeQtPtr<QHBoxLayout>();
  templateRow->setSpacing(8);
  auto templateLabel = makeQtPtr<QLabel>(this);
  templateLabel->setObjectName(QStringLiteral("ControlsEditorTemplateLabel"));
  templateLabel->setTextFormat(Qt::PlainText);
  templateLabel_ = templateLabel.get();
  templateRow->addWidget(templateLabel.get());

  auto combo = makeQtPtr<QComboBox>(this);
  combo->setObjectName(QStringLiteral("ControlsEditorTemplates"));
  combo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
  combo->setMinimumWidth(0);
  templateCombo_ = combo.get();
  templateRow->addWidget(combo.get(), 1);

  auto loadButton = makeQtPtr<QPushButton>(this);
  loadButton->setObjectName(QStringLiteral("ControlsEditorLoad"));
  Style::setVariant(loadButton.get(), "ghost");
  Style::setSize(loadButton.get(), "sm");
  loadButton_ = loadButton.get();
  templateRow->addWidget(loadButton.get());

  auto removeButton = makeQtPtr<QPushButton>(this);
  removeButton->setObjectName(QStringLiteral("ControlsEditorRemoveTemplate"));
  Style::setVariant(removeButton.get(), "ghost");
  Style::setSize(removeButton.get(), "sm");
  removeButton_ = removeButton.get();
  templateRow->addWidget(removeButton.get());
  columnLayout->addLayout(templateRow.get());

  auto nameRow = makeQtPtr<QHBoxLayout>();
  nameRow->setSpacing(8);
  auto nameLabel = makeQtPtr<QLabel>(this);
  nameLabel->setObjectName(QStringLiteral("ControlsEditorTemplateNameLabel"));
  nameLabel->setTextFormat(Qt::PlainText);
  templateNameLabel_ = nameLabel.get();
  nameRow->addWidget(nameLabel.get());

  auto nameEdit = makeQtPtr<QLineEdit>(this);
  nameEdit->setObjectName(QStringLiteral("ControlsEditorTemplateName"));
  nameEdit->setMinimumWidth(0);
  nameEdit->setClearButtonEnabled(true);
  templateNameEdit_ = nameEdit.get();
  nameRow->addWidget(nameEdit.get(), 1);

  auto saveTemplate = makeQtPtr<QPushButton>(this);
  saveTemplate->setObjectName(QStringLiteral("ControlsEditorSaveTemplate"));
  Style::setVariant(saveTemplate.get(), "ghost");
  Style::setSize(saveTemplate.get(), "sm");
  saveTemplateButton_ = saveTemplate.get();
  nameRow->addWidget(saveTemplate.get());
  columnLayout->addLayout(nameRow.get());

  auto status = makeQtPtr<QLabel>(this);
  status->setObjectName(QStringLiteral("ControlsEditorTemplateStatus"));
  status->setWordWrap(true);
  status->setTextFormat(Qt::PlainText);
  status->setMinimumWidth(0);
  Style::setRole(status.get(), "muted");
  templateStatus_ = status.get();
  columnLayout->addWidget(status.get());

  connect(loadButton_, &QPushButton::clicked, this, [this]() { onLoadTemplateClicked(); });
  connect(removeButton_, &QPushButton::clicked, this, [this]() { onRemoveTemplateClicked(); });
  connect(saveTemplateButton_, &QPushButton::clicked, this, [this]() { onSaveTemplateClicked(); });
  connect(templateCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
          [this](int) { updateRemoveTemplateEnabled(); });
}

void GameControlsEditorDialog::buildEditorBody(QLayout* rootLayout) {
  auto* columnLayout = qobject_cast<QVBoxLayout*>(rootLayout);
  if (!columnLayout) return;

  auto splitter = makeQtPtr<QSplitter>(Qt::Horizontal, this);
  splitter->setObjectName(QStringLiteral("ControlsEditorSplitter"));
  splitter->setChildrenCollapsible(false);
  splitter_ = splitter.get();

  auto gridScroll = makeQtPtr<QScrollArea>(splitter.get());
  gridScroll->setObjectName(QStringLiteral("ControlsEditorGridScroll"));
  gridScroll->setWidgetResizable(true);
  gridScroll->setFrameShape(QFrame::NoFrame);
  gridScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  gridScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  gridScroll->setMinimumWidth(0);

  auto gridHost = makeQtPtr<QWidget>();
  gridHost->setObjectName(QStringLiteral("ControlsEditorGrid"));
  gridHost->setMinimumWidth(0);
  gridHost_ = gridHost.get();
  buildGrid(gridHost.get());
  gridScroll->setWidget(gridHost.release());

  auto detailScroll = makeQtPtr<QScrollArea>(splitter.get());
  detailScroll->setObjectName(QStringLiteral("ControlsEditorDetailScroll"));
  detailScroll->setWidgetResizable(true);
  detailScroll->setFrameShape(QFrame::NoFrame);
  detailScroll->setMinimumWidth(0);
  auto detailHost = makeQtPtr<QWidget>();
  detailHost->setObjectName(QStringLiteral("ControlsEditorDetail"));
  detailHost->setMinimumWidth(0);
  buildDetail(detailHost.get());
  detailScroll->setWidget(detailHost.release());

  splitter->addWidget(gridScroll.get());
  splitter->addWidget(detailScroll.get());
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  columnLayout->addWidget(splitter.get(), 1);
}

void GameControlsEditorDialog::buildGrid(QWidget* host) {
  auto column = makeQtPtr<QVBoxLayout>(host);
  column->setContentsMargins(0, 0, 12, 0);
  column->setSpacing(8);

  auto caption = makeQtPtr<QLabel>(host);
  caption->setObjectName(QStringLiteral("ControlsEditorGridCaption"));
  caption->setTextFormat(Qt::PlainText);
  caption->setWordWrap(true);
  Style::setRole(caption.get(), "muted");
  gridCaption_ = caption.get();
  column->addWidget(caption.get());

  int maximumLeftOffsetUnits = 0;
  const int rowCount = EventCatalog::buttonGridRowCount();
  for (int rowIndex = 0; rowIndex < rowCount; ++rowIndex) {
    maximumLeftOffsetUnits = qMax(maximumLeftOffsetUnits,
                                  EventCatalog::buttonGridRowAt(rowIndex).leftOffsetUnits);
  }
  for (int rowIndex = 0; rowIndex < rowCount; ++rowIndex) {
    const EventCatalog::ButtonGridRow gridRow = EventCatalog::buttonGridRowAt(rowIndex);
    QList<QPushButton*> rowButtons;
    for (int columnIndex = 0; columnIndex < gridRow.eventCount; ++columnIndex) {
      const EventCatalog::Node& node =
          EventCatalog::buttonGridNodeAt(gridRow.firstEventIndex + columnIndex);
      auto button = makeQtPtr<QPushButton>(host);
      button->setObjectName(QStringLiteral("ControlsGrid%1").arg(static_cast<int>(node.id)));
      button->setProperty("gameEventId", static_cast<int>(node.id));
      button->setProperty("gameEventName", QString::fromUtf8(node.canonicalName));
      button->setText(QString());
      Style::setSize(button.get(), "md");
      Style::setVariant(button.get(), "gameControl");
      button->setFocusPolicy(Qt::ClickFocus);
      button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
      button->setMinimumWidth(0);
      button->setMinimumHeight(52);
      button->installEventFilter(this);

      auto buttonLayout = makeQtPtr<QVBoxLayout>(button.get());
      buttonLayout->setContentsMargins(2, 4, 2, 4);
      buttonLayout->setSpacing(2);
      auto title = makeQtPtr<QLabel>(button.get());
      title->setProperty("gameEventId", static_cast<int>(node.id));
      title->setTextFormat(Qt::PlainText);
      title->setAlignment(Qt::AlignCenter);
      title->setWordWrap(true);
      title->setMinimumWidth(0);
      title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
      Style::setRole(title.get(), "gameControlTitle");
      title->setAttribute(Qt::WA_TransparentForMouseEvents, true);
      auto shortcut = makeQtPtr<QLabel>(button.get());
      shortcut->setProperty("shortcutCaption", true);
      shortcut->setTextFormat(Qt::PlainText);
      shortcut->setAlignment(Qt::AlignCenter);
      shortcut->setMinimumWidth(0);
      shortcut->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
      Style::setRole(shortcut.get(), "muted");
      shortcut->setAttribute(Qt::WA_TransparentForMouseEvents, true);
      buttonLayout->addWidget(title.get());
      buttonLayout->addWidget(shortcut.get());

      connect(button.get(), &QPushButton::clicked, this, [this, eventId = node.id]() {
        selectEvent(eventId, false);
      });
      rowButtons.append(button.get());
      gridButtons_.append(button.get());
    }
    addKeyboardRow(column.get(), host, rowButtons, gridRow.leftOffsetUnits, maximumLeftOffsetUnits);
  }
  column->addStretch(1);
}

void GameControlsEditorDialog::buildDetail(QWidget* host) {
  auto column = makeQtPtr<QVBoxLayout>(host);
  column->setContentsMargins(12, 0, 0, 0);
  column->setSpacing(8);

  auto nameLabel = makeQtPtr<QLabel>(host);
  nameLabel->setObjectName(QStringLiteral("ControlsEditorNameLabel"));
  nameLabel->setTextFormat(Qt::PlainText);
  nameLabel_ = nameLabel.get();
  column->addWidget(nameLabel.get());

  auto nameEdit = makeQtPtr<QLineEdit>(host);
  nameEdit->setObjectName(QStringLiteral("ControlsEditorName"));
  nameEdit->setMinimumWidth(0);
  nameEdit->setClearButtonEnabled(true);
  nameEdit_ = nameEdit.get();
  column->addWidget(nameEdit.get());

  auto nameError = makeQtPtr<QLabel>(host);
  nameError->setObjectName(QStringLiteral("ControlsEditorNameError"));
  nameError->setWordWrap(true);
  nameError->setTextFormat(Qt::PlainText);
  nameError->setMinimumWidth(0);
  Style::setRole(nameError.get(), "inlineError");
  nameError_ = nameError.get();
  column->addWidget(nameError.get());

  auto keyLabel = makeQtPtr<QLabel>(host);
  keyLabel->setObjectName(QStringLiteral("ControlsEditorKeyLabel"));
  keyLabel->setTextFormat(Qt::PlainText);
  keyLabel_ = keyLabel.get();
  column->addWidget(keyLabel.get());

  auto keyEdit = makeQtPtr<QLineEdit>(host);
  keyEdit->setObjectName(QStringLiteral("ControlsEditorKey"));
  keyEdit->setMinimumWidth(0);
  keyEdit_ = keyEdit.get();
  column->addWidget(keyEdit.get());

  auto keyError = makeQtPtr<QLabel>(host);
  keyError->setObjectName(QStringLiteral("ControlsEditorKeyError"));
  keyError->setWordWrap(true);
  keyError->setTextFormat(Qt::PlainText);
  keyError->setMinimumWidth(0);
  Style::setRole(keyError.get(), "inlineError");
  keyError_ = keyError.get();
  column->addWidget(keyError.get());

  auto shownBox = makeQtPtr<QCheckBox>(host);
  shownBox->setObjectName(QStringLiteral("ControlsEditorShown"));
  shownBox_ = shownBox.get();
  column->addWidget(shownBox.get());

  auto heading = makeQtPtr<QLabel>(host);
  heading->setObjectName(QStringLiteral("ControlsEditorFollowUpHeading"));
  heading->setWordWrap(true);
  heading->setTextFormat(Qt::PlainText);
  heading->setMinimumWidth(0);
  Style::setRole(heading.get(), "h2");
  followUpHeading_ = heading.get();
  column->addWidget(heading.get());

  auto count = makeQtPtr<QLabel>(host);
  count->setObjectName(QStringLiteral("ControlsEditorFollowUpCount"));
  count->setTextFormat(Qt::PlainText);
  count->setWordWrap(true);
  Style::setRole(count.get(), "muted");
  followUpCount_ = count.get();
  column->addWidget(count.get());

  auto empty = makeQtPtr<QLabel>(host);
  empty->setObjectName(QStringLiteral("ControlsEditorFollowUpEmpty"));
  empty->setWordWrap(true);
  empty->setTextFormat(Qt::PlainText);
  empty->setMinimumWidth(0);
  followUpEmpty_ = empty.get();
  column->addWidget(empty.get());

  auto tree = makeQtPtr<QTreeWidget>(host);
  tree->setObjectName(QStringLiteral("ControlsEditorFollowUps"));
  tree->setColumnCount(2);
  tree->setTextElideMode(Qt::ElideMiddle);
  tree->setIndentation(22);
  tree->setMinimumWidth(0);
  tree->setUniformRowHeights(true);
  tree->setRootIsDecorated(true);
  tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  tree->header()->setStretchLastSection(false);
  followUpTree_ = tree.get();
  column->addWidget(tree.get(), 1);

  auto detail = makeQtPtr<QLabel>(host);
  detail->setObjectName(QStringLiteral("ControlsEditorFollowUpDetail"));
  detail->setWordWrap(true);
  detail->setTextFormat(Qt::PlainText);
  detail->setMinimumWidth(0);
  followUpDetail_ = detail.get();
  column->addWidget(detail.get());

  connect(nameEdit_, &QLineEdit::textChanged, this,
          [this](const QString& text) { onNameEdited(text); });
  connect(keyEdit_, &QLineEdit::textChanged, this,
          [this](const QString& text) { onKeyEdited(text); });
  connect(shownBox_, &QCheckBox::toggled, this, [this](bool shown) { onShownToggled(shown); });
  connect(followUpTree_, &QTreeWidget::itemChanged, this,
          [this](QTreeWidgetItem* item, int columnIndex) {
            onFollowUpItemChanged(item, columnIndex);
          });
  connect(followUpTree_, &QTreeWidget::currentItemChanged, this,
          [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
            if (!followUpDetail_ || suppressDetailEdits_) return;
            if (!current) {
              followUpDetail_->clear();
              return;
            }
            const auto eventId =
                static_cast<EventCatalog::EventId>(current->data(0, Qt::UserRole).toInt());
            const QString full = displayLabel(eventId, draft_, pendingLabels_);
            followUpDetail_->setText(full);
            followUpDetail_->setToolTip(full);
          });
}

void GameControlsEditorDialog::buildBreakToggle() {
  if (!qEnvironmentVariableIsSet("AVA_BREAK_UI")) return;
  auto toggle = makeQtPtr<QWidget>(this);
  toggle->setObjectName(QStringLiteral("BreakUiToggle"));
  Style::setRole(toggle.get(), "breakUiToggle");
  auto row = makeQtPtr<QHBoxLayout>(toggle.get());
  row->setContentsMargins(4, 4, 4, 4);
  row->setSpacing(2);

  const std::array<std::pair<const char*, DataFixture>, 4> segments = {{
      {"BreakUiDemo", DataFixture::Session},
      {"BreakUiWorst", DataFixture::WorstCase},
      {"BreakUiEmpty", DataFixture::EmptyFollowUps},
      {"BreakUiOne", DataFixture::OneFollowUp},
  }};
  for (const auto& segment : segments) {
    auto button = makeQtPtr<QPushButton>(toggle.get());
    button->setObjectName(QString::fromUtf8(segment.first));
    button->setCheckable(true);
    button->setProperty("fixture", static_cast<int>(segment.second));
    Style::setRole(button.get(), "breakUiSegment");
    button->setChecked(segment.second == DataFixture::Session);
    breakSegmentButtons_.insert(static_cast<int>(segment.second), button.get());
    connect(button.get(), &QPushButton::clicked, this, [this, fixture = segment.second]() {
      applyDataFixture(fixture);
    });
    row->addWidget(button.get());
  }
  breakToggle_ = toggle.get();
  positionBreakToggle();
}

void GameControlsEditorDialog::applyUiStrings() {
  setWindowTitle(AppLocale::trUi("controls_editor.title"));
  if (titleLabel_) titleLabel_->setText(AppLocale::trUi("controls_editor.title"));
  if (subtitleLabel_) subtitleLabel_->setText(AppLocale::trUi("controls_editor.subtitle"));
  if (templateLabel_) templateLabel_->setText(AppLocale::trUi("controls_editor.template"));
  if (loadButton_) loadButton_->setText(AppLocale::trUi("controls_editor.load"));
  if (removeButton_) removeButton_->setText(AppLocale::trUi("controls_editor.remove_template"));
  if (templateNameLabel_) {
    templateNameLabel_->setText(AppLocale::trUi("controls_editor.template_name"));
  }
  if (templateNameEdit_) {
    templateNameEdit_->setPlaceholderText(AppLocale::trUi("controls_editor.template_name"));
  }
  if (saveTemplateButton_) {
    saveTemplateButton_->setText(AppLocale::trUi("controls_editor.save_template"));
  }
  if (gridCaption_) gridCaption_->setText(AppLocale::trUi("controls_editor.layout_caption"));
  if (nameLabel_) nameLabel_->setText(AppLocale::trUi("controls_editor.name"));
  if (keyLabel_) keyLabel_->setText(AppLocale::trUi("controls_editor.key"));
  if (shownBox_) shownBox_->setText(AppLocale::trUi("controls_editor.shown_while_tagging"));
  if (followUpEmpty_) followUpEmpty_->setText(AppLocale::trUi("controls_editor.follow_ups_none"));
  if (resetButton_) resetButton_->setText(AppLocale::trUi("controls_editor.reset"));
  if (cancelButton_) cancelButton_->setText(AppLocale::trUi("controls_editor.cancel"));
  if (saveButton_) saveButton_->setText(AppLocale::trUi("controls_editor.save"));
  if (followUpTree_) {
    QTreeWidgetItem* header = followUpTree_->headerItem();
    if (header) {
      header->setText(0, AppLocale::trUi("controls_editor.column_name"));
      header->setText(1, AppLocale::trUi("controls_editor.shown"));
    }
  }
  const QHash<int, const char*> segmentKeys = {
      {static_cast<int>(DataFixture::Session), "controls_editor.break_demo"},
      {static_cast<int>(DataFixture::WorstCase), "controls_editor.break_worst"},
      {static_cast<int>(DataFixture::EmptyFollowUps), "controls_editor.break_empty"},
      {static_cast<int>(DataFixture::OneFollowUp), "controls_editor.break_one"},
  };
  for (auto iterator = breakSegmentButtons_.cbegin(); iterator != breakSegmentButtons_.cend();
       ++iterator) {
    if (!iterator.value()) continue;
    const auto key = segmentKeys.constFind(iterator.key());
    if (key != segmentKeys.cend()) iterator.value()->setText(AppLocale::trUi(key.value()));
  }

  const QString selectedTemplate =
      templateCombo_ ? templateCombo_->currentData().toString() : QString();
  if (templateCombo_) reloadTemplateList(selectedTemplate);
  updateAllGridButtons();
  if (selectedEventId_ != EventCatalog::EventId::None) loadDetail();
  updateBanner();
  updateSaveEnabled();
  positionBreakToggle();
}

void GameControlsEditorDialog::applyDataFixture(DataFixture fixture) {
  pendingLabels_.clear();
  pendingKeys_.clear();
  if (fixture == DataFixture::Session) {
    draft_ = tagSession_ ? EventControlsSetup::fromSession(*tagSession_).labelOverrides
                         : EventLabelOverrides::Table{};
  } else if (fixture == DataFixture::WorstCase) {
    draft_ = EventControlsSetup::builtinDefault().labelOverrides;
    const std::array<std::pair<EventCatalog::EventId, const char*>, 10> labels = {{
        {EventCatalog::EventId::Shot, "Aleksandra Wiśniewska-Kowalczyk"},
        {EventCatalog::EventId::Goal, "J"},
        {EventCatalog::EventId::Pass, "Đặng Thị Ngọc Hân"},
        {EventCatalog::EventId::CircleEntry, "María José de la Cruz y Fernández"},
        {EventCatalog::EventId::Card, "Benachrichtigungseinstellungen"},
        {EventCatalog::EventId::PenaltyCorner, "customer-feedback-from-enterprise-onboarding"},
        {EventCatalog::EventId::Turnover, "Jo"},
        {EventCatalog::EventId::Special, "🦊 Fox"},
        {EventCatalog::EventId::PenaltyCornerFoul, "<script>alert(1)</script>"},
        {EventCatalog::EventId::OnTarget, "Konstantin Oberhauser-Wettstein"},
    }};
    for (const auto& entry : labels) {
      EventControlsValidation::Issue issue;
      const QString text = QString::fromUtf8(entry.second);
      if (!EventControlsValidation::commitLabel(&draft_, entry.first, text, &issue)) {
        pendingLabels_.insert(static_cast<int>(entry.first), text);
      }
    }
    const QString spaces = QStringLiteral("   ");
    pendingLabels_.insert(static_cast<int>(EventCatalog::EventId::SixteenYard), spaces);
    pendingLabels_.insert(static_cast<int>(EventCatalog::EventId::SeventyFiveYard),
                          QStringLiteral("Line one\nLine two"));
    pendingKeys_.insert(static_cast<int>(EventCatalog::EventId::FiftyYard), QStringLiteral("Q"));
    pendingKeys_.insert(static_cast<int>(EventCatalog::EventId::PenaltyStroke), QStringLiteral("G"));
    EventControlsValidation::commitShown(&draft_, EventCatalog::EventId::Closeby, false, nullptr);
  } else if (fixture == DataFixture::EmptyFollowUps) {
    draft_ = EventControlsSetup::builtinDefault().labelOverrides;
  } else if (fixture == DataFixture::OneFollowUp) {
    draft_ = EventControlsSetup::builtinDefault().labelOverrides;
    const std::array<EventCatalog::EventId, 7> hiddenFollowUps = {
        EventCatalog::EventId::OffTarget,     EventCatalog::EventId::Blocked,
        EventCatalog::EventId::FollowUpGoal,  EventCatalog::EventId::Saved,
        EventCatalog::EventId::Post,          EventCatalog::EventId::Closeby,
        EventCatalog::EventId::NotClose,
    };
    for (const EventCatalog::EventId eventId : hiddenFollowUps) {
      EventControlsValidation::commitShown(&draft_, eventId, false, nullptr);
    }
  }

  for (auto iterator = breakSegmentButtons_.cbegin(); iterator != breakSegmentButtons_.cend();
       ++iterator) {
    if (iterator.value()) iterator.value()->setChecked(iterator.key() == static_cast<int>(fixture));
  }

  EventCatalog::EventId eventId = EventCatalog::EventId::Shot;
  if (fixture == DataFixture::EmptyFollowUps) eventId = EventCatalog::EventId::FiftyYard;
  if (fixture == DataFixture::WorstCase) eventId = EventCatalog::EventId::SixteenYard;
  if (fixture == DataFixture::Session && selectedEventId_ != EventCatalog::EventId::None) {
    eventId = selectedEventId_;
  }
  updateAllGridButtons();
  selectEvent(eventId, true);
  updateBanner();
  updateSaveEnabled();
}

void GameControlsEditorDialog::reloadTemplateList(const QString& selectedStableName) {
  if (!templateCombo_) return;
  const EventControlsTemplateStore::Catalog catalog =
      EventControlsTemplateStore::listTemplates(templateStorageRoot_);
  const QString keep =
      selectedStableName.isEmpty() ? templateCombo_->currentData().toString() : selectedStableName;
  QSignalBlocker blocker(templateCombo_);
  templateCombo_->clear();
  for (const EventControlsTemplateStore::ListedTemplate& listing : catalog.templates) {
    const QString display = templateDisplayName(listing);
    templateCombo_->addItem(display, listing.name);
    templateCombo_->setItemData(templateCombo_->count() - 1, display, Qt::ToolTipRole);
  }
  int index = templateCombo_->findData(keep);
  if (index < 0) index = 0;
  templateCombo_->setCurrentIndex(index);
  templateCombo_->setToolTip(templateCombo_->currentText());
  updateRemoveTemplateEnabled();
  if (!templateStatus_) return;
  if (catalog.fileStatus == EventControlsTemplateStore::FileStatus::Corrupt) {
    templateStatus_->setText(AppLocale::trUi("controls_editor.template_corrupt"));
    Style::setRole(templateStatus_, "inlineError");
  } else if (catalog.fileStatus == EventControlsTemplateStore::FileStatus::NewerVersion) {
    templateStatus_->setText(AppLocale::trUi("controls_editor.template_newer"));
    Style::setRole(templateStatus_, "inlineError");
  }
}

void GameControlsEditorDialog::selectEvent(EventCatalog::EventId eventId, bool force) {
  if (!force && eventId == selectedEventId_) return;
  if (!force && !currentEventIsClean()) {
    updateBanner();
    return;
  }
  selectedEventId_ = eventId;
  loadDetail();
  updateGridSelection();
}

void GameControlsEditorDialog::loadDetail() {
  if (selectedEventId_ == EventCatalog::EventId::None) return;
  suppressDetailEdits_ = true;
  const int eventKey = static_cast<int>(selectedEventId_);
  if (nameEdit_) {
    const auto pending = pendingLabels_.constFind(eventKey);
    nameEdit_->setText(pending == pendingLabels_.cend()
                           ? EventLabelResolver::labelForId(selectedEventId_, draft_)
                           : pending.value());
  }
  if (keyEdit_) {
    const auto pending = pendingKeys_.constFind(eventKey);
    keyEdit_->setText(pending == pendingKeys_.cend()
                          ? EventControlsValidation::letterForKey(
                                EventControlsValidation::effectiveShortcut(selectedEventId_, draft_))
                          : pending.value());
  }
  if (shownBox_) shownBox_->setChecked(!draft_.hidden(selectedEventId_));
  suppressDetailEdits_ = false;
  rebuildFollowUpTree();
  updateFollowUpSummary();
  updateBanner();
  updateSaveEnabled();
}

void GameControlsEditorDialog::rememberPendingLabel(EventCatalog::EventId eventId,
                                                    const QString& text, bool pending) {
  const int eventKey = static_cast<int>(eventId);
  if (pending) pendingLabels_.insert(eventKey, text);
  else pendingLabels_.remove(eventKey);
}

void GameControlsEditorDialog::rememberPendingKey(EventCatalog::EventId eventId,
                                                  const QString& text, bool pending) {
  const int eventKey = static_cast<int>(eventId);
  if (pending) pendingKeys_.insert(eventKey, text);
  else pendingKeys_.remove(eventKey);
}

void GameControlsEditorDialog::onNameEdited(const QString& text) {
  if (suppressDetailEdits_ || selectedEventId_ == EventCatalog::EventId::None) return;
  EventControlsValidation::Issue issue;
  const bool accepted =
      EventControlsValidation::commitLabel(&draft_, selectedEventId_, text, &issue);
  rememberPendingLabel(selectedEventId_, text, !accepted);
  if (nameError_) {
    nameError_->setText(accepted ? QString()
                                 : EventControlsValidation::issueMessage(issue, draft_));
  }
  if (accepted) updateGridButton(gridButtonFor(selectedEventId_));
  updateFollowUpSummary();
  updateBanner();
  updateSaveEnabled();
}

void GameControlsEditorDialog::onKeyEdited(const QString& text) {
  if (suppressDetailEdits_ || selectedEventId_ == EventCatalog::EventId::None) return;
  EventControlsValidation::Issue issue;
  const bool accepted =
      EventControlsValidation::commitShortcutText(&draft_, selectedEventId_, text, &issue);
  rememberPendingKey(selectedEventId_, text, !accepted);
  if (keyError_) {
    keyError_->setText(accepted ? QString() : EventControlsValidation::issueMessage(issue, draft_));
  }
  if (accepted) updateGridButton(gridButtonFor(selectedEventId_));
  updateBanner();
  updateSaveEnabled();
}

void GameControlsEditorDialog::onShownToggled(bool shown) {
  if (suppressDetailEdits_ || selectedEventId_ == EventCatalog::EventId::None || !shownBox_) return;
  if (!shown) {
    const QString title = AppLocale::trUi("controls_menu.hide_title");
    const QString message = AppLocale::trUi("controls_menu.hide_message");
    if (!GameControlsContextMenu::confirmDestructive(this, title, message)) {
      suppressDetailEdits_ = true;
      shownBox_->setChecked(true);
      suppressDetailEdits_ = false;
      return;
    }
  }
  EventControlsValidation::Issue issue;
  if (!EventControlsValidation::commitShown(&draft_, selectedEventId_, shown, &issue)) {
    suppressDetailEdits_ = true;
    shownBox_->setChecked(!draft_.hidden(selectedEventId_));
    suppressDetailEdits_ = false;
    if (nameError_) nameError_->setText(EventControlsValidation::issueMessage(issue, draft_));
    updateBanner();
    updateSaveEnabled();
    return;
  }
  updateGridButton(gridButtonFor(selectedEventId_));
  updateBanner();
  updateSaveEnabled();
}

void GameControlsEditorDialog::onFollowUpItemChanged(QTreeWidgetItem* item, int column) {
  if (suppressDetailEdits_ || !item) return;
  const auto eventId = static_cast<EventCatalog::EventId>(item->data(0, Qt::UserRole).toInt());
  if (eventId == EventCatalog::EventId::None) return;
  if (column == 0) {
    EventControlsValidation::Issue issue;
    const QString text = item->text(0);
    const bool accepted = EventControlsValidation::commitLabel(&draft_, eventId, text, &issue);
    rememberPendingLabel(eventId, text, !accepted);
    const QString full = displayLabel(eventId, draft_, pendingLabels_);
    item->setToolTip(0, full);
    if (followUpDetail_) {
      followUpDetail_->setText(full);
      followUpDetail_->setToolTip(full);
    }
    if (accepted && item->text(0) != EventLabelResolver::labelForId(eventId, draft_)) {
      suppressDetailEdits_ = true;
      item->setText(0, EventLabelResolver::labelForId(eventId, draft_));
      suppressDetailEdits_ = false;
    }
    updateFollowUpSummary();
    updateBanner();
    updateSaveEnabled();
    return;
  }
  if (column != 1) return;
  const bool shown = item->checkState(1) == Qt::Checked;
  const bool wasShown = !draft_.hidden(eventId);
  if (shown == wasShown) return;
  if (!shown) {
    if (!GameControlsContextMenu::confirmDestructive(
            this, AppLocale::trUi("controls_menu.hide_follow_up_title"),
            AppLocale::trUi("controls_menu.hide_follow_up_message"))) {
      suppressDetailEdits_ = true;
      item->setCheckState(1, Qt::Checked);
      suppressDetailEdits_ = false;
      return;
    }
  }
  EventControlsValidation::Issue issue;
  if (!EventControlsValidation::commitShown(&draft_, eventId, shown, &issue)) {
    suppressDetailEdits_ = true;
    item->setCheckState(1, wasShown ? Qt::Checked : Qt::Unchecked);
    suppressDetailEdits_ = false;
    if (followUpDetail_) {
      followUpDetail_->setText(EventControlsValidation::issueMessage(issue, draft_));
    }
    updateBanner();
    updateSaveEnabled();
    return;
  }
  item->setForeground(0, shown ? QBrush() : QBrush(Style::ThemeColors::faint()));
  updateFollowUpSummary();
  updateBanner();
  updateSaveEnabled();
}

void GameControlsEditorDialog::onSaveClicked() {
  if (!canSave() || !tagSession_) return;
  EventControlsSetup::Setup setup;
  setup.labelOverrides = draft_;
  if (EventControlsSetup::applyToSession(*tagSession_, setup) !=
      EventControlsSetup::ApplyStatus::Applied) {
    if (banner_) {
      banner_->setText(AppLocale::trUi("controls_editor.apply_failed"));
      banner_->setVisible(true);
    }
    return;
  }
  accept();
}

void GameControlsEditorDialog::onResetClicked() {
  if (!GameControlsContextMenu::confirmDestructive(
          this, AppLocale::trUi("controls_editor.reset_title"),
          AppLocale::trUi("controls_editor.reset_message"))) {
    return;
  }
  draft_ = EventControlsSetup::builtinDefault().labelOverrides;
  pendingLabels_.clear();
  pendingKeys_.clear();
  updateAllGridButtons();
  const EventCatalog::EventId firstEvent =
      EventCatalog::buttonGridCount() > 0 ? EventCatalog::buttonGridNodeAt(0).id
                                          : EventCatalog::EventId::None;
  const EventCatalog::EventId keep =
      selectedEventId_ == EventCatalog::EventId::None ? firstEvent : selectedEventId_;
  selectEvent(keep, true);
}

void GameControlsEditorDialog::onLoadTemplateClicked() {
  if (!templateCombo_) return;
  const QString stableName = templateCombo_->currentData().toString();
  if (stableName.isEmpty()) return;
  if (draftDiffersFromSession()) {
    if (!GameControlsContextMenu::confirmDestructive(
            this, AppLocale::trUi("controls_editor.load_confirm_title"),
            AppLocale::trUi("controls_editor.load_confirm_message"))) {
      return;
    }
  }
  const EventControlsTemplateStore::LoadResult loaded =
      EventControlsTemplateStore::loadTemplate(stableName, templateStorageRoot_);
  if (loaded.status != EventControlsTemplateStore::LoadStatus::Loaded) {
    if (!templateStatus_) return;
    Style::setRole(templateStatus_, "inlineError");
    if (loaded.status == EventControlsTemplateStore::LoadStatus::Corrupt) {
      templateStatus_->setText(AppLocale::trUi("controls_editor.template_corrupt"));
    } else if (loaded.status == EventControlsTemplateStore::LoadStatus::NewerVersion) {
      templateStatus_->setText(AppLocale::trUi("controls_editor.template_newer"));
    } else if (loaded.status == EventControlsTemplateStore::LoadStatus::Missing) {
      templateStatus_->setText(AppLocale::trUi("controls_editor.template_missing"));
    } else {
      templateStatus_->setText(AppLocale::trUi("controls_editor.template_io"));
    }
    return;
  }
  draft_ = loaded.setup.labelOverrides;
  pendingLabels_.clear();
  pendingKeys_.clear();
  if (templateStatus_) {
    Style::setRole(templateStatus_, "muted");
    templateStatus_->clear();
  }
  updateAllGridButtons();
  selectEvent(selectedEventId_ == EventCatalog::EventId::None
                  ? EventCatalog::buttonGridNodeAt(0).id
                  : selectedEventId_,
              true);
}

void GameControlsEditorDialog::onSaveTemplateClicked() {
  if (!canSave() || !templateNameEdit_ || !templateStatus_) return;
  const QString name = templateNameEdit_->text();
  if (name.trimmed().isEmpty()) {
    Style::setRole(templateStatus_, "inlineError");
    templateStatus_->setText(AppLocale::trUi("controls_editor.template_empty"));
    return;
  }
  if (EventControlsSetup::isBuiltinDefaultName(name)) {
    Style::setRole(templateStatus_, "inlineError");
    templateStatus_->setText(AppLocale::trUi("controls_editor.template_builtin"));
    return;
  }
  const EventControlsTemplateStore::Catalog catalog =
      EventControlsTemplateStore::listTemplates(templateStorageRoot_);
  for (const EventControlsTemplateStore::ListedTemplate& listing : catalog.templates) {
    if (listing.builtin || listing.name != name.trimmed()) continue;
    if (!GameControlsContextMenu::confirmDestructive(
            this, AppLocale::trUi("controls_editor.template_replace_title"),
            AppLocale::trUi("controls_editor.template_replace_message").arg(name.trimmed()))) {
      return;
    }
    break;
  }
  EventControlsSetup::Setup setup;
  setup.labelOverrides = draft_;
  const EventControlsTemplateStore::SaveResult saved =
      EventControlsTemplateStore::saveTemplate(name, setup, templateStorageRoot_);
  if (saved.status != EventControlsTemplateStore::SaveStatus::Saved) {
    Style::setRole(templateStatus_, "inlineError");
    if (saved.status == EventControlsTemplateStore::SaveStatus::BuiltinName) {
      templateStatus_->setText(AppLocale::trUi("controls_editor.template_builtin"));
    } else if (saved.status == EventControlsTemplateStore::SaveStatus::InvalidName) {
      templateStatus_->setText(name.trimmed().isEmpty()
                                   ? AppLocale::trUi("controls_editor.template_empty")
                                   : AppLocale::trUi("controls_editor.template_invalid"));
    } else if (saved.status == EventControlsTemplateStore::SaveStatus::Corrupt) {
      templateStatus_->setText(AppLocale::trUi("controls_editor.template_corrupt"));
    } else if (saved.status == EventControlsTemplateStore::SaveStatus::NewerVersion) {
      templateStatus_->setText(AppLocale::trUi("controls_editor.template_newer"));
    } else {
      templateStatus_->setText(AppLocale::trUi("controls_editor.template_io"));
    }
    return;
  }
  Style::setRole(templateStatus_, "muted");
  templateStatus_->setText(AppLocale::trUi("controls_editor.template_saved").arg(name.trimmed()));
  reloadTemplateList(name.trimmed());
}

void GameControlsEditorDialog::onRemoveTemplateClicked() {
  if (!templateCombo_) return;
  const QString stableName = templateCombo_->currentData().toString();
  if (stableName.isEmpty() || EventControlsSetup::isBuiltinDefaultName(stableName)) return;
  if (!GameControlsContextMenu::confirmDestructive(
          this, AppLocale::trUi("controls_editor.remove_template_title"),
          AppLocale::trUi("controls_editor.remove_template_message").arg(stableName))) {
    return;
  }
  const EventControlsTemplateStore::RemoveResult removed =
      EventControlsTemplateStore::removeTemplate(stableName, templateStorageRoot_);
  if (!templateStatus_) {
    reloadTemplateList(EventControlsSetup::builtinDefaultName());
    return;
  }
  if (removed.status != EventControlsTemplateStore::RemoveStatus::Removed) {
    Style::setRole(templateStatus_, "inlineError");
    templateStatus_->setText(removed.status == EventControlsTemplateStore::RemoveStatus::BuiltinName
                                 ? AppLocale::trUi("controls_editor.template_builtin")
                                 : AppLocale::trUi("controls_editor.template_io"));
    return;
  }
  Style::setRole(templateStatus_, "muted");
  templateStatus_->clear();
  reloadTemplateList(EventControlsSetup::builtinDefaultName());
}

QPushButton* GameControlsEditorDialog::gridButtonFor(EventCatalog::EventId eventId) const {
  for (const QPointer<QPushButton>& button : gridButtons_) {
    if (!button) continue;
    if (button->property("gameEventId").toInt() == static_cast<int>(eventId)) return button;
  }
  return nullptr;
}

void GameControlsEditorDialog::updateGridButton(QPushButton* button) {
  if (!button) return;
  const auto eventId = static_cast<EventCatalog::EventId>(button->property("gameEventId").toInt());
  const bool hidden = draft_.hidden(eventId);
  Style::setState(button, "controlsHidden", hidden);
  Style::setState(button, "activeMain", eventId == selectedEventId_);
  const QString full = EventLabelResolver::labelForId(eventId, draft_);
  const QString letter = EventControlsValidation::letterForKey(
      EventControlsValidation::effectiveShortcut(eventId, draft_));
  button->setAccessibleName(
      hidden ? AppLocale::trUi("controls_editor.hidden_access").arg(full) : full);
  for (QLabel* label : button->findChildren<QLabel*>(Qt::FindDirectChildrenOnly)) {
    if (label->property("shortcutCaption").toBool()) {
      label->setText(letter);
      continue;
    }
    label->setProperty("fullLabel", full);
    label->setTextFormat(Qt::PlainText);
  }
  refitButton(button);
}

void GameControlsEditorDialog::updateAllGridButtons() {
  for (const QPointer<QPushButton>& button : gridButtons_) updateGridButton(button);
}

void GameControlsEditorDialog::updateGridSelection() {
  for (const QPointer<QPushButton>& button : gridButtons_) {
    if (!button) continue;
    const auto eventId = static_cast<EventCatalog::EventId>(button->property("gameEventId").toInt());
    Style::setState(button, "activeMain", eventId == selectedEventId_);
  }
}

void GameControlsEditorDialog::refitButton(QPushButton* button) {
  if (!button) return;
  for (QLabel* label : button->findChildren<QLabel*>(Qt::FindDirectChildrenOnly)) {
    if (label->property("shortcutCaption").toBool()) continue;
    if (!label->property("fullLabel").isValid()) continue;
    fitTitleLabel(label);
  }
}

void GameControlsEditorDialog::refitGridLabels() {
  for (const QPointer<QPushButton>& button : gridButtons_) refitButton(button);
}

void GameControlsEditorDialog::rebuildFollowUpTree() {
  if (!followUpTree_) return;
  const EventCatalog::Node* root = EventCatalog::nodeForId(selectedEventId_);
  QSignalBlocker blocker(followUpTree_);
  followUpTree_->clear();
  if (!root) return;

  std::function<void(QTreeWidgetItem*, const EventCatalog::Node&)> addChildren =
      [&](QTreeWidgetItem* parentItem, const EventCatalog::Node& node) {
        for (int childIndex = 0; childIndex < node.childCount; ++childIndex) {
          const EventCatalog::EventId childId =
              node.children[static_cast<std::size_t>(childIndex)];
          const EventCatalog::Node* child = EventCatalog::nodeForId(childId);
          if (!child) continue;
          auto* item = parentItem ? new QTreeWidgetItem(parentItem) : new QTreeWidgetItem(followUpTree_);
          const QString full = displayLabel(childId, draft_, pendingLabels_);
          item->setData(0, Qt::UserRole, static_cast<int>(childId));
          item->setText(0, full);
          item->setToolTip(0, full);
          item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable |
                         Qt::ItemIsSelectable | Qt::ItemIsEnabled);
          const bool shown = !draft_.hidden(childId);
          item->setCheckState(1, shown ? Qt::Checked : Qt::Unchecked);
          item->setToolTip(1, AppLocale::trUi("controls_editor.shown"));
          if (!shown) item->setForeground(0, QBrush(Style::ThemeColors::faint()));
          addChildren(item, *child);
        }
      };
  addChildren(nullptr, *root);
  followUpTree_->expandAll();
  if (followUpTree_->topLevelItemCount() > 0) {
    followUpTree_->setCurrentItem(followUpTree_->topLevelItem(0));
  }
}

void GameControlsEditorDialog::updateFollowUpSummary() {
  const EventCatalog::Node* root = EventCatalog::nodeForId(selectedEventId_);
  const QString label = selectedEventId_ == EventCatalog::EventId::None
                            ? QString()
                            : displayLabel(selectedEventId_, draft_, pendingLabels_);
  if (followUpHeading_) {
    followUpHeading_->setText(AppLocale::trUi("controls_editor.follow_ups_for").arg(label));
    followUpHeading_->setToolTip(label);
  }
  int total = 0;
  int shown = 0;
  if (root) accumulateFollowUps(*root, draft_, &total, &shown);
  if (followUpCount_) followUpCount_->setText(followUpCountText(shown, total));
  const bool empty = total <= 0;
  if (followUpEmpty_) followUpEmpty_->setVisible(empty);
  if (followUpTree_) followUpTree_->setVisible(!empty);
  if (followUpDetail_) followUpDetail_->setVisible(!empty);
  if (nameError_ && selectedEventId_ != EventCatalog::EventId::None) {
    const int eventKey = static_cast<int>(selectedEventId_);
    if (!pendingLabels_.contains(eventKey)) nameError_->clear();
  }
  if (keyError_ && selectedEventId_ != EventCatalog::EventId::None) {
    const int eventKey = static_cast<int>(selectedEventId_);
    if (!pendingKeys_.contains(eventKey)) keyError_->clear();
    else if (keyEdit_) {
      const EventControlsValidation::Issue issue = EventControlsValidation::inspectShortcutText(
          draft_, selectedEventId_, keyEdit_->text());
      keyError_->setText(EventControlsValidation::issueMessage(issue, draft_));
    }
  }
  if (nameError_ && nameEdit_ && pendingLabels_.contains(static_cast<int>(selectedEventId_))) {
    const EventControlsValidation::Issue issue =
        EventControlsValidation::inspectLabel(draft_, selectedEventId_, nameEdit_->text());
    nameError_->setText(EventControlsValidation::issueMessage(issue, draft_));
  }
}

void GameControlsEditorDialog::updateBanner() {
  if (!banner_) return;
  QStringList messages;
  for (const EventControlsValidation::Issue& issue :
       EventControlsValidation::reviewStoredKeys(draft_)) {
    appendIssueMessage(&messages, issue, draft_);
  }
  for (auto iterator = pendingLabels_.cbegin(); iterator != pendingLabels_.cend(); ++iterator) {
    const auto eventId = static_cast<EventCatalog::EventId>(iterator.key());
    appendIssueMessage(&messages,
                       EventControlsValidation::inspectLabel(draft_, eventId, iterator.value()),
                       draft_);
  }
  for (auto iterator = pendingKeys_.cbegin(); iterator != pendingKeys_.cend(); ++iterator) {
    const auto eventId = static_cast<EventCatalog::EventId>(iterator.key());
    appendIssueMessage(
        &messages, EventControlsValidation::inspectShortcutText(draft_, eventId, iterator.value()),
        draft_);
  }
  banner_->setText(messages.join(QStringLiteral("\n")));
  banner_->setVisible(!messages.isEmpty());
}

void GameControlsEditorDialog::updateSaveEnabled() {
  const bool enabled = canSave();
  if (saveButton_) {
    saveButton_->setEnabled(enabled);
    saveButton_->setToolTip(enabled ? QString() : AppLocale::trUi("controls_editor.fix_before_save"));
  }
  if (saveTemplateButton_) saveTemplateButton_->setEnabled(enabled);
}

void GameControlsEditorDialog::updateRemoveTemplateEnabled() {
  if (!removeButton_ || !templateCombo_) return;
  const QString stableName = templateCombo_->currentData().toString();
  const bool builtin =
      stableName.isEmpty() || EventControlsSetup::isBuiltinDefaultName(stableName);
  removeButton_->setEnabled(!builtin);
}

void GameControlsEditorDialog::positionBreakToggle() {
  if (!breakToggle_) return;
  breakToggle_->adjustSize();
  const QSize hint = breakToggle_->sizeHint();
  const int toggleWidth = qMax(hint.width(), 280);
  const int x = qMax(0, (width() - toggleWidth) / 2);
  const int y = qMax(0, height() - hint.height() - 12);
  breakToggle_->setGeometry(x, y, toggleWidth, qMax(hint.height(), 36));
  breakToggle_->raise();
}

bool GameControlsEditorDialog::canSave() const {
  if (!tagSession_) return false;
  if (!pendingLabels_.isEmpty() || !pendingKeys_.isEmpty()) return false;
  return EventControlsValidation::reviewStoredKeys(draft_).isEmpty();
}

bool GameControlsEditorDialog::currentEventIsClean() const {
  const int eventKey = static_cast<int>(selectedEventId_);
  return !pendingLabels_.contains(eventKey) && !pendingKeys_.contains(eventKey);
}

bool GameControlsEditorDialog::draftDiffersFromSession() const {
  if (!pendingLabels_.isEmpty() || !pendingKeys_.isEmpty()) return true;
  if (!tagSession_) return draft_ != EventLabelOverrides::Table{};
  return draft_ != tagSession_->eventLabelOverrides();
}

void GameControlsEditorDialog::showEvent(QShowEvent* event) {
  QDialog::showEvent(event);
  refitGridLabels();
  positionBreakToggle();
}

void GameControlsEditorDialog::resizeEvent(QResizeEvent* event) {
  QDialog::resizeEvent(event);
  refitGridLabels();
  positionBreakToggle();
}

bool GameControlsEditorDialog::eventFilter(QObject* watched, QEvent* event) {
  if (event->type() == QEvent::Resize) {
    if (auto* button = qobject_cast<QPushButton*>(watched)) refitButton(button);
  }
  return QDialog::eventFilter(watched, event);
}
