#include "ExportJobsBar.h"

#include "AppLocale.h"
#include "QtPtr.h"
#include "StyleProps.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QVBoxLayout>

namespace {

void clearLayoutItems(QLayout* layout) {
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        } else if (QLayout* childLayout = item->layout()) {
            clearLayoutItems(childLayout);
            delete childLayout;
        }
        delete item;
    }
}

}  // namespace

ExportJobsBar::ExportJobsBar(ExportJobManager* manager, QWidget* parent)
    : QWidget(parent)
    , manager_(manager) {
    setObjectName(QStringLiteral("ExportJobsBar"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(12, 8, 12, 8);
    rootLayout->setSpacing(6);

    rowsLayout_ = new QVBoxLayout();
    rowsLayout_->setContentsMargins(0, 0, 0, 0);
    rowsLayout_->setSpacing(6);
    rootLayout->addLayout(rowsLayout_);

    if (manager_) {
        connect(manager_, &ExportJobManager::jobsChanged, this, &ExportJobsBar::syncRows,
                Qt::QueuedConnection);
    }
    hide();
}

void ExportJobsBar::applyUiStrings() {
    for (const JobRowWidgets& widgets : rowsByJobId_) {
        applyRowUiStrings(widgets);
    }
}

void ExportJobsBar::clearAllRows() {
    rowsByJobId_.clear();
    clearLayoutItems(rowsLayout_);
}

void ExportJobsBar::syncRows() {
    if (!manager_ || !manager_->hasJobs()) {
        clearAllRows();
        hide();
        return;
    }

    const QVector<ExportJobSnapshot> snapshots = manager_->snapshots();
    QSet<int> activeJobIds;
    for (const ExportJobSnapshot& snapshot : snapshots) {
        activeJobIds.insert(snapshot.id);
    }

    for (auto iterator = rowsByJobId_.begin(); iterator != rowsByJobId_.end();) {
        if (!activeJobIds.contains(iterator.key())) {
            if (iterator.value().row) {
                rowsLayout_->removeWidget(iterator.value().row);
                iterator.value().row->deleteLater();
            }
            iterator = rowsByJobId_.erase(iterator);
        } else {
            ++iterator;
        }
    }

    for (const ExportJobSnapshot& snapshot : snapshots) {
        JobRowWidgets& widgets = rowsByJobId_[snapshot.id];
        if (widgets.row) {
            updateRow(widgets, snapshot);
            continue;
        }
        widgets = createRow(snapshot);
    }

    show();
}

ExportJobsBar::JobRowWidgets ExportJobsBar::createRow(const ExportJobSnapshot& snapshot) {
    JobRowWidgets widgets;

    auto row = makeQtPtr<QWidget>(this);
    row->setObjectName(QStringLiteral("ExportJobRow"));
    auto rowLayout = makeQtPtr<QHBoxLayout>(row.get());
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(8);

    auto nameLabel = makeQtPtr<QLabel>(snapshot.displayName, row.get());
    nameLabel->setMinimumWidth(160);
    rowLayout->addWidget(nameLabel.get(), 0);
    widgets.nameLabel = nameLabel.get();

    auto statusLabel = makeQtPtr<QLabel>(snapshot.statusText, row.get());
    Style::setRole(statusLabel.get(), "muted");
    statusLabel->setWordWrap(true);
    rowLayout->addWidget(statusLabel.get(), 1);
    widgets.statusLabel = statusLabel.get();

    auto progressBar = makeQtPtr<QProgressBar>(row.get());
    progressBar->setRange(0, 100);
    progressBar->setFixedWidth(140);
    progressBar->setTextVisible(true);
    rowLayout->addWidget(progressBar.get(), 0);
    widgets.progressBar = progressBar.get();

    auto cancelButton = makeQtPtr<QPushButton>(AppLocale::trUi("export.cancel"), row.get());
    cancelButton->setCursor(Qt::PointingHandCursor);
    Style::setVariant(cancelButton.get(), "destructive");
    Style::setSize(cancelButton.get(), "sm");
    const int jobId = snapshot.id;
    connect(cancelButton.get(), &QPushButton::clicked, this,
            [jobId, mgr = QPointer<ExportJobManager>(manager_)]() {
                if (mgr) mgr->cancelJob(jobId);
            });
    rowLayout->addWidget(cancelButton.get(), 0);
    widgets.cancelButton = cancelButton.get();

    auto dismissButton = makeQtPtr<QPushButton>(AppLocale::trUi("export.job_dismiss"), row.get());
    dismissButton->setCursor(Qt::PointingHandCursor);
    Style::setVariant(dismissButton.get(), "ghost");
    Style::setSize(dismissButton.get(), "sm");
    connect(dismissButton.get(), &QPushButton::clicked, this,
            [jobId, mgr = QPointer<ExportJobManager>(manager_)]() {
                if (mgr) mgr->dismissJob(jobId);
            });
    rowLayout->addWidget(dismissButton.get(), 0);
    widgets.dismissButton = dismissButton.get();

    rowsLayout_->addWidget(row.get());
    widgets.row = row.get();

    updateRow(widgets, snapshot);
    return widgets;
}

void ExportJobsBar::updateRow(JobRowWidgets& widgets, const ExportJobSnapshot& snapshot) {
    if (widgets.nameLabel) widgets.nameLabel->setText(snapshot.displayName);
    if (widgets.statusLabel) widgets.statusLabel->setText(snapshot.statusText);
    if (widgets.progressBar) widgets.progressBar->setValue(snapshot.progressPercent);
    if (widgets.cancelButton) widgets.cancelButton->setVisible(snapshot.canCancel);
    if (widgets.dismissButton) widgets.dismissButton->setVisible(snapshot.canDismiss);
}

void ExportJobsBar::applyRowUiStrings(const JobRowWidgets& widgets) {
    if (widgets.cancelButton) {
        widgets.cancelButton->setText(AppLocale::trUi("export.cancel"));
    }
    if (widgets.dismissButton) {
        widgets.dismissButton->setText(AppLocale::trUi("export.job_dismiss"));
    }
}
