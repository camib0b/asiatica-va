#include "ExportJobsBar.h"

#include "AppLocale.h"
#include "ExportJobManager.h"
#include "QtPtr.h"
#include "StyleProps.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

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
        connect(manager_, &ExportJobManager::jobsChanged, this, &ExportJobsBar::rebuildRows,
                Qt::QueuedConnection);
    }
    hide();
}

void ExportJobsBar::applyUiStrings() {
    rebuildRows();
}

void ExportJobsBar::rebuildRows() {
    while (QLayoutItem* item = rowsLayout_->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }

    if (!manager_ || !manager_->hasJobs()) {
        hide();
        return;
    }

    const auto snapshots = manager_->snapshots();
    for (const ExportJobSnapshot& snapshot : snapshots) {
        auto row = makeQtPtr<QWidget>(this);
        row->setObjectName(QStringLiteral("ExportJobRow"));
        auto rowLayout = makeQtPtr<QHBoxLayout>(row.get());
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);

        auto nameLabel = makeQtPtr<QLabel>(snapshot.displayName, row.get());
        nameLabel->setMinimumWidth(160);
        rowLayout->addWidget(nameLabel.get(), 0);

        auto statusLabel = makeQtPtr<QLabel>(snapshot.statusText, row.get());
        Style::setRole(statusLabel.get(), "muted");
        statusLabel->setWordWrap(true);
        rowLayout->addWidget(statusLabel.get(), 1);

        auto progressBar = makeQtPtr<QProgressBar>(row.get());
        progressBar->setRange(0, 100);
        progressBar->setValue(snapshot.progressPercent);
        progressBar->setFixedWidth(140);
        progressBar->setTextVisible(true);
        rowLayout->addWidget(progressBar.get(), 0);

        if (snapshot.canCancel) {
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
        }

        if (snapshot.canDismiss) {
            auto dismissButton =
                makeQtPtr<QPushButton>(AppLocale::trUi("export.job_dismiss"), row.get());
            dismissButton->setCursor(Qt::PointingHandCursor);
            Style::setVariant(dismissButton.get(), "ghost");
            Style::setSize(dismissButton.get(), "sm");
            const int jobId = snapshot.id;
            connect(dismissButton.get(), &QPushButton::clicked, this,
                    [jobId, mgr = QPointer<ExportJobManager>(manager_)]() {
                        if (mgr) mgr->dismissJob(jobId);
                    });
            rowLayout->addWidget(dismissButton.get(), 0);
        }

        rowsLayout_->addWidget(row.get());
    }

    show();
}
