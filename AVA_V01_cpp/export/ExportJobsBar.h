#pragma once

#include <QPointer>
#include <QWidget>

class QVBoxLayout;
class ExportJobManager;

class ExportJobsBar final : public QWidget {
    Q_OBJECT

public:
    explicit ExportJobsBar(ExportJobManager* manager, QWidget* parent = nullptr);

    void applyUiStrings();

private:
    void rebuildRows();

    QPointer<ExportJobManager> manager_;
    QVBoxLayout* rowsLayout_ = nullptr;
};
