#pragma once

#include "ExportJobManager.h"

#include <QHash>
#include <QPointer>
#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;
class QVBoxLayout;
class ExportCheckMark;
class ExportJobRow;

class ExportJobsBar final : public QWidget {
    Q_OBJECT

public:
    explicit ExportJobsBar(ExportJobManager* manager, QWidget* parent = nullptr);

    void applyUiStrings() const;

private:
    enum class RowPhase {
        Running,
        Succeeded,
        Failed,
        Cancelled,
    };

    struct JobRowWidgets {
        QPointer<ExportJobRow> row;
        QPointer<ExportCheckMark> checkMark;
        QPointer<QLabel> nameLabel;
        QPointer<QLabel> statusLabel;
        QPointer<QProgressBar> progressBar;
        QPointer<QPushButton> openFolderButton;
        QPointer<QPushButton> cancelButton;
        QPointer<QPushButton> dismissButton;
        RowPhase phase = RowPhase::Running;
        bool celebrated = false;
        QString outputDirectory;
    };

    void syncRows();
    void clearAllRows();
    JobRowWidgets createRow(const ExportJobSnapshot& snapshot);
    void updateRow(JobRowWidgets& widgets, const ExportJobSnapshot& snapshot);
    void playSuccess(JobRowWidgets& widgets);
    void syncProgress(const JobRowWidgets& widgets, const ExportJobSnapshot& snapshot) const;
    void applyRowUiStrings(const JobRowWidgets& widgets) const;

    QPointer<ExportJobManager> manager_;
    QVBoxLayout* rowsLayout_ = nullptr;
    QHash<int, JobRowWidgets> rowsByJobId_;
};
