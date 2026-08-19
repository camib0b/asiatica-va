#include "ExportJobsBar.h"

#include "AppLocale.h"
#include "ExportJobManager.h"
#include "StyleProps.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QUrl>
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
        connect(manager_, &ExportJobManager::jobsChanged, this, &ExportJobsBar::rebuildRows);
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
        auto* row = new QWidget(this);
        row->setObjectName(QStringLiteral("ExportJobRow"));
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);

        auto* nameLabel = new QLabel(snapshot.displayName, row);
        nameLabel->setMinimumWidth(160);
        rowLayout->addWidget(nameLabel, 0);

        auto* statusLabel = new QLabel(snapshot.statusText, row);
        Style::setRole(statusLabel, snapshot.failed ? "muted" : "muted");
        statusLabel->setWordWrap(true);
        rowLayout->addWidget(statusLabel, 1);

        auto* progressBar = new QProgressBar(row);
        progressBar->setRange(0, 100);
        progressBar->setValue(snapshot.progressPercent);
        progressBar->setFixedWidth(140);
        progressBar->setTextVisible(true);
        rowLayout->addWidget(progressBar, 0);

        if (!snapshot.youtubeUrl.isEmpty()) {
            auto* openButton = new QPushButton(AppLocale::trUi("export.youtube_open_video"), row);
            openButton->setCursor(Qt::PointingHandCursor);
            Style::setVariant(openButton, "outline");
            Style::setSize(openButton, "sm");
            const QString videoUrl = snapshot.youtubeUrl;
            connect(openButton, &QPushButton::clicked, this, [videoUrl]() {
                QDesktopServices::openUrl(QUrl(videoUrl));
            });
            rowLayout->addWidget(openButton, 0);
        }

        if (snapshot.canCancel) {
            auto* cancelButton = new QPushButton(AppLocale::trUi("export.cancel"), row);
            cancelButton->setCursor(Qt::PointingHandCursor);
            Style::setVariant(cancelButton, "destructive");
            Style::setSize(cancelButton, "sm");
            const int jobId = snapshot.id;
            connect(cancelButton, &QPushButton::clicked, this, [this, jobId]() {
                if (manager_) manager_->cancelJob(jobId);
            });
            rowLayout->addWidget(cancelButton, 0);
        }

        if (snapshot.canDismiss) {
            auto* dismissButton = new QPushButton(AppLocale::trUi("export.job_dismiss"), row);
            dismissButton->setCursor(Qt::PointingHandCursor);
            Style::setVariant(dismissButton, "ghost");
            Style::setSize(dismissButton, "sm");
            const int jobId = snapshot.id;
            connect(dismissButton, &QPushButton::clicked, this, [this, jobId]() {
                if (manager_) manager_->dismissJob(jobId);
            });
            rowLayout->addWidget(dismissButton, 0);
        }

        rowsLayout_->addWidget(row);
    }

    show();
}
