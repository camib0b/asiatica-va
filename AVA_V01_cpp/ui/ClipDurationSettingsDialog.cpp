#include "ClipDurationSettingsDialog.h"

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

namespace {

constexpr double kMinDurationSeconds = 0.0;
constexpr double kMaxDurationSeconds = 60.0;
constexpr double kDurationStepSeconds = 0.5;

QDoubleSpinBox* makeDurationSpinBox(QWidget* parent) {
  auto* spin = new QDoubleSpinBox(parent);
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
    : QDialog(parent), tagSession_(session) {
  setWindowTitle(AppLocale::trUi("clip_durations.title"));
  setMinimumSize(560, 520);
  resize(600, 560);

  buildUi();
  populateRows();
  applyUiStrings();
}

void ClipDurationSettingsDialog::buildUi() {
  auto* rootLayout = new QVBoxLayout(this);
  rootLayout->setSpacing(16);
  rootLayout->setContentsMargins(24, 24, 24, 24);

  titleLabel_ = new QLabel(this);
  Style::setRole(titleLabel_, "h2");
  rootLayout->addWidget(titleLabel_);

  subtitleLabel_ = new QLabel(this);
  Style::setRole(subtitleLabel_, "muted");
  subtitleLabel_->setWordWrap(true);
  rootLayout->addWidget(subtitleLabel_);

  auto* scrollArea = new QScrollArea(this);
  scrollArea->setWidgetResizable(true);
  scrollArea->setFrameShape(QFrame::NoFrame);
  scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  auto* tableHost = new QWidget(scrollArea);
  auto* grid = new QGridLayout(tableHost);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(12);
  grid->setVerticalSpacing(8);

  eventHeaderLabel_ = new QLabel(tableHost);
  Style::setRole(eventHeaderLabel_, "muted");
  leadHeaderLabel_ = new QLabel(tableHost);
  Style::setRole(leadHeaderLabel_, "muted");
  leadHeaderLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  lagHeaderLabel_ = new QLabel(tableHost);
  Style::setRole(lagHeaderLabel_, "muted");
  lagHeaderLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  totalHeaderLabel_ = new QLabel(tableHost);
  Style::setRole(totalHeaderLabel_, "muted");
  totalHeaderLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

  grid->addWidget(eventHeaderLabel_, 0, 0);
  grid->addWidget(leadHeaderLabel_, 0, 1);
  grid->addWidget(lagHeaderLabel_, 0, 2);
  grid->addWidget(totalHeaderLabel_, 0, 3);
  grid->setColumnStretch(0, 1);

  scrollArea->setWidget(tableHost);
  rootLayout->addWidget(scrollArea, 1);

  auto* buttonRow = new QHBoxLayout();
  buttonRow->setSpacing(8);

  resetButton_ = new QPushButton(this);
  resetButton_->setCursor(Qt::PointingHandCursor);
  Style::setVariant(resetButton_, "outline");
  connect(resetButton_, &QPushButton::clicked, this, &ClipDurationSettingsDialog::onResetAllClicked);
  buttonRow->addWidget(resetButton_);

  buttonRow->addStretch(1);

  closeButton_ = new QPushButton(this);
  closeButton_->setCursor(Qt::PointingHandCursor);
  closeButton_->setDefault(true);
  Style::setVariant(closeButton_, "primary");
  connect(closeButton_, &QPushButton::clicked, this, &QDialog::accept);
  buttonRow->addWidget(closeButton_);

  rootLayout->addLayout(buttonRow);
}

void ClipDurationSettingsDialog::populateRows() {
  QWidget* tableHost = eventHeaderLabel_->parentWidget();
  auto* grid = qobject_cast<QGridLayout*>(tableHost->layout());
  if (!grid) return;

  for (const DurationRow& row : rows_) {
    if (row.eventLabel) grid->removeWidget(row.eventLabel);
    if (row.leadSpin) grid->removeWidget(row.leadSpin);
    if (row.lagSpin) grid->removeWidget(row.lagSpin);
    if (row.totalLabel) grid->removeWidget(row.totalLabel);
    delete row.eventLabel;
    delete row.leadSpin;
    delete row.lagSpin;
    delete row.totalLabel;
  }
  rows_.clear();

  const QStringList eventTypes = EventDefaults::allConfigurableEventTypes();
  rows_.reserve(eventTypes.size());

  int gridRow = 1;
  for (const QString& eventName : eventTypes) {
    const EventDefaults::EventDuration duration = EventDefaults::defaultFor(eventName);

    DurationRow row;
    row.eventName = eventName;
    row.eventLabel = new QLabel(AppLocale::trEvent(eventName), tableHost);

    row.leadSpin = makeDurationSpinBox(tableHost);
    row.leadSpin->setValue(duration.preMs / 1000.0);

    row.lagSpin = makeDurationSpinBox(tableHost);
    row.lagSpin->setValue(duration.postMs / 1000.0);

    row.totalLabel = new QLabel(tableHost);
    row.totalLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    Style::setRole(row.totalLabel, "muted");
    refreshTotalLabel(row);

    grid->addWidget(row.eventLabel, gridRow, 0);
    grid->addWidget(row.leadSpin, gridRow, 1);
    grid->addWidget(row.lagSpin, gridRow, 2);
    grid->addWidget(row.totalLabel, gridRow, 3);

    connect(row.leadSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this, eventName](double) { onDurationChanged(eventName); });
    connect(row.lagSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
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
                                                         qint64 preMs,
                                                         qint64 postMs) {
  if (!tagSession_) return;
  tagSession_->applyDefaultsToUntrimmedTags(eventName, preMs, postMs);
}

void ClipDurationSettingsDialog::onDurationChanged(const QString& eventName) {
  for (DurationRow& row : rows_) {
    if (row.eventName != eventName) continue;
    if (!row.leadSpin || !row.lagSpin) return;

    const qint64 preMs = static_cast<qint64>(row.leadSpin->value() * 1000.0);
    const qint64 postMs = static_cast<qint64>(row.lagSpin->value() * 1000.0);
    EventDefaults::setUserOverride(eventName, preMs, postMs);
    refreshTotalLabel(row);
    applyDurationToSession(eventName, preMs, postMs);
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
      row.leadSpin->setValue(factoryDefault.preMs / 1000.0);
      row.lagSpin->setValue(factoryDefault.postMs / 1000.0);
    }

    refreshTotalLabel(row);
    applyDurationToSession(row.eventName, factoryDefault.preMs, factoryDefault.postMs);
  }
}
