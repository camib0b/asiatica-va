#include "ClipDurationSettingsDialog.h"
#include "QtPtr.h"

#include "../i18n/AppLocale.h"
#include "../state/EventDefaults.h"
#include "../state/TagSession.h"
#include "../style/StyleProps.h"

#include <QAbstractSpinBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <memory>

namespace {

constexpr double kMinDurationSeconds = 0.0;
constexpr double kMaxDurationSeconds = 60.0;
constexpr double kDurationStepSeconds = 0.5;

std::unique_ptr<QDoubleSpinBox, QtParentDeleter> makeDurationSpinBox(QWidget* parent) {
  auto spin = makeQtPtr<QDoubleSpinBox>(parent);
  spin->setRange(kMinDurationSeconds, kMaxDurationSeconds);
  spin->setSingleStep(kDurationStepSeconds);
  spin->setDecimals(1);
  spin->setSuffix(QStringLiteral(" s"));
  spin->setMinimumWidth(96);
  spin->setMinimumHeight(36);
  spin->setAlignment(Qt::AlignRight);
  spin->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
  return spin;
}

} // namespace

ClipDurationSettingsDialog::ClipDurationSettingsDialog(TagSession* session, QWidget* parent)
    : QDialog(parent),
      tagSession_(session),
      tableHost_(nullptr),
      tableGrid_(nullptr),
      titleLabel_(nullptr),
      subtitleLabel_(nullptr),
      eventHeaderLabel_(nullptr),
      leadHeaderLabel_(nullptr),
      lagHeaderLabel_(nullptr),
      totalHeaderLabel_(nullptr),
      resetButton_(nullptr),
      closeButton_(nullptr) {
  setWindowTitle(AppLocale::trUi("clip_durations.title"));
  setMinimumSize(560, 520);
  resize(600, 560);

  buildUi();
  populateRows();
  applyUiStrings();
}

void ClipDurationSettingsDialog::buildUi() {
  auto rootLayout = makeQtPtr<QVBoxLayout>(this);
  rootLayout->setSpacing(16);
  rootLayout->setContentsMargins(24, 24, 24, 24);

  auto titleLabel = makeQtPtr<QLabel>(this);
  Style::setRole(titleLabel.get(), "h2");
  titleLabel_ = titleLabel.get();
  rootLayout->addWidget(titleLabel.get());

  auto subtitleLabel = makeQtPtr<QLabel>(this);
  Style::setRole(subtitleLabel.get(), "muted");
  subtitleLabel->setWordWrap(true);
  subtitleLabel_ = subtitleLabel.get();
  rootLayout->addWidget(subtitleLabel.get());

  auto scrollArea = makeQtPtr<QScrollArea>(this);
  scrollArea->setWidgetResizable(true);
  scrollArea->setFrameShape(QFrame::NoFrame);
  scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  auto tableHost = makeQtPtr<QWidget>(scrollArea.get());
  auto grid = makeQtPtr<QGridLayout>(tableHost.get());
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(12);
  grid->setVerticalSpacing(8);
  tableHost_ = tableHost.get();
  tableGrid_ = grid.get();

  auto eventHeaderLabel = makeQtPtr<QLabel>(tableHost.get());
  Style::setRole(eventHeaderLabel.get(), "muted");
  eventHeaderLabel_ = eventHeaderLabel.get();

  auto leadHeaderLabel = makeQtPtr<QLabel>(tableHost.get());
  Style::setRole(leadHeaderLabel.get(), "muted");
  leadHeaderLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  leadHeaderLabel_ = leadHeaderLabel.get();

  auto lagHeaderLabel = makeQtPtr<QLabel>(tableHost.get());
  Style::setRole(lagHeaderLabel.get(), "muted");
  lagHeaderLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  lagHeaderLabel_ = lagHeaderLabel.get();

  auto totalHeaderLabel = makeQtPtr<QLabel>(tableHost.get());
  Style::setRole(totalHeaderLabel.get(), "muted");
  totalHeaderLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  totalHeaderLabel_ = totalHeaderLabel.get();

  grid->addWidget(eventHeaderLabel.get(), 0, 0);
  grid->addWidget(leadHeaderLabel.get(), 0, 1);
  grid->addWidget(lagHeaderLabel.get(), 0, 2);
  grid->addWidget(totalHeaderLabel.get(), 0, 3);
  grid->setColumnStretch(0, 1);

  scrollArea->setWidget(tableHost.get());
  rootLayout->addWidget(scrollArea.get(), 1);

  auto buttonRow = makeQtPtr<QHBoxLayout>();
  buttonRow->setSpacing(8);

  auto resetButton = makeQtPtr<QPushButton>(this);
  resetButton->setCursor(Qt::PointingHandCursor);
  Style::setVariant(resetButton.get(), "outline");
  connect(resetButton.get(), &QPushButton::clicked, this,
          &ClipDurationSettingsDialog::onResetAllClicked);
  resetButton_ = resetButton.get();
  buttonRow->addWidget(resetButton.get());

  buttonRow->addStretch(1);

  auto closeButton = makeQtPtr<QPushButton>(this);
  closeButton->setCursor(Qt::PointingHandCursor);
  closeButton->setDefault(true);
  Style::setVariant(closeButton.get(), "primary");
  connect(closeButton.get(), &QPushButton::clicked, this, &QDialog::accept);
  closeButton_ = closeButton.get();
  buttonRow->addWidget(closeButton.get());

  rootLayout->addLayout(buttonRow.get());
}

void ClipDurationSettingsDialog::populateRows() {
  if (!tableHost_ || !tableGrid_) return;
  QGridLayout* grid = tableGrid_.get();

  for (DurationRow& row : rows_) {
    unparentAndDelete(row.eventLabel.get());
    unparentAndDelete(row.leadSpin.get());
    unparentAndDelete(row.lagSpin.get());
    unparentAndDelete(row.totalLabel.get());
  }
  rows_.clear();

  const QStringList eventTypes = EventDefaults::allConfigurableEventTypes();
  rows_.reserve(eventTypes.size());

  int gridRow = 1;
  for (const QString& eventName : eventTypes) {
    const EventDefaults::EventDuration duration = EventDefaults::defaultFor(eventName);

    auto eventLabel = makeQtPtr<QLabel>(AppLocale::trEvent(eventName), tableHost_.get());
    auto leadSpin = makeDurationSpinBox(tableHost_.get());
    leadSpin->setValue(duration.leadMs / 1000.0);
    auto lagSpin = makeDurationSpinBox(tableHost_.get());
    lagSpin->setValue(duration.lagMs / 1000.0);
    auto totalLabel = makeQtPtr<QLabel>(tableHost_.get());
    totalLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    Style::setRole(totalLabel.get(), "muted");

    DurationRow row;
    row.eventName = eventName;
    row.eventLabel = eventLabel.get();
    row.leadSpin = leadSpin.get();
    row.lagSpin = lagSpin.get();
    row.totalLabel = totalLabel.get();
    refreshTotalLabel(row);

    grid->addWidget(eventLabel.get(), gridRow, 0);
    grid->addWidget(leadSpin.get(), gridRow, 1);
    grid->addWidget(lagSpin.get(), gridRow, 2);
    grid->addWidget(totalLabel.get(), gridRow, 3);

    connect(leadSpin.get(), QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this, eventName](double) { onDurationChanged(eventName); });
    connect(lagSpin.get(), QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this, eventName](double) { onDurationChanged(eventName); });

    rows_.append(row);
    ++gridRow;
  }
}

void ClipDurationSettingsDialog::applyUiStrings() {
  setWindowTitle(AppLocale::trUi("clip_durations.title"));
  if (titleLabel_) titleLabel_->setText(AppLocale::trUi("clip_durations.title"));
  if (subtitleLabel_) subtitleLabel_->setText(AppLocale::trUi("clip_durations.subtitle"));
  if (eventHeaderLabel_) eventHeaderLabel_->setText(AppLocale::trUi("clip_durations.col_event"));
  if (leadHeaderLabel_) leadHeaderLabel_->setText(AppLocale::trUi("clip_durations.col_lead"));
  if (lagHeaderLabel_) lagHeaderLabel_->setText(AppLocale::trUi("clip_durations.col_lag"));
  if (totalHeaderLabel_) totalHeaderLabel_->setText(AppLocale::trUi("clip_durations.col_total"));
  if (resetButton_) resetButton_->setText(AppLocale::trUi("clip_durations.reset"));
  if (closeButton_) closeButton_->setText(AppLocale::trUi("clip_durations.close"));

  for (DurationRow& row : rows_) {
    if (row.eventLabel) row.eventLabel->setText(AppLocale::trEvent(row.eventName));
    refreshTotalLabel(row);
  }
}

void ClipDurationSettingsDialog::refreshTotalLabel(const DurationRow& row) {
  if (!row.totalLabel || !row.leadSpin || !row.lagSpin) return;
  const double totalSeconds = row.leadSpin->value() + row.lagSpin->value();
  row.totalLabel->setText(QStringLiteral("%1 s").arg(totalSeconds, 0, 'f', 1));
}

void ClipDurationSettingsDialog::applyDurationToSession(const QString& eventName,
                                                         qint64 leadMs,
                                                         qint64 lagMs) {
  if (!tagSession_) return;
  tagSession_->applyDefaultsToUntrimmedTags(eventName, leadMs, lagMs);
}

void ClipDurationSettingsDialog::onDurationChanged(const QString& eventName) {
  for (DurationRow& row : rows_) {
    if (row.eventName != eventName) continue;
    if (!row.leadSpin || !row.lagSpin) return;

    const qint64 leadMs = static_cast<qint64>(row.leadSpin->value() * 1000.0);
    const qint64 lagMs = static_cast<qint64>(row.lagSpin->value() * 1000.0);
    EventDefaults::setUserOverride(eventName, leadMs, lagMs);
    refreshTotalLabel(row);
    applyDurationToSession(eventName, leadMs, lagMs);
    return;
  }
}

void ClipDurationSettingsDialog::onResetAllClicked() {
  EventDefaults::clearUserOverrides();

  for (DurationRow& row : rows_) {
    if (!row.leadSpin || !row.lagSpin) continue;
    const EventDefaults::EventDuration factoryDefault =
        EventDefaults::factoryDefaultFor(row.eventName);

    {
      const QSignalBlocker leadBlocker(row.leadSpin);
      const QSignalBlocker lagBlocker(row.lagSpin);
      row.leadSpin->setValue(factoryDefault.leadMs / 1000.0);
      row.lagSpin->setValue(factoryDefault.lagMs / 1000.0);
    }

    refreshTotalLabel(row);
    applyDurationToSession(row.eventName, factoryDefault.leadMs, factoryDefault.lagMs);
  }
}
