#pragma once

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

    ExportJobManager* manager_ = nullptr;
    QVBoxLayout* rowsLayout_ = nullptr;
};
