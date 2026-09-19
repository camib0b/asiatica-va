#pragma once

#include "ExportJobManager.h"

#include <QHash>
#include <QPointer>
#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;
class QVBoxLayout;

class ExportJobsBar final : public QWidget {
    Q_OBJECT

public:
    explicit ExportJobsBar(ExportJobManager* manager, QWidget* parent = nullptr);

    void applyUiStrings() const;

private:
    struct JobRowWidgets {
        QPointer<QWidget> row;
        QPointer<QLabel> nameLabel;
        QPointer<QLabel> statusLabel;
        QPointer<QProgressBar> progressBar;
        QPointer<QPushButton> cancelButton;
        QPointer<QPushButton> dismissButton;
    };

    void syncRows();
    void clearAllRows();
    JobRowWidgets createRow(const ExportJobSnapshot& snapshot);
    void updateRow(JobRowWidgets& widgets, const ExportJobSnapshot& snapshot);
    void applyRowUiStrings(const JobRowWidgets& widgets) const;

    QPointer<ExportJobManager> manager_;
    QVBoxLayout* rowsLayout_ = nullptr;
    QHash<int, JobRowWidgets> rowsByJobId_;
};
