#pragma once

#include <QDialog>
#include <QPointer>
#include <QStringList>

class QListWidget;

class ConcatFileOrderDialog final : public QDialog {
    Q_OBJECT

public:
    explicit ConcatFileOrderDialog(const QStringList& filePaths, QWidget* parent = nullptr);

    QStringList orderedFilePaths() const;

private slots:
    void moveCurrentItemLeft();
    void moveCurrentItemRight();

private:
    void buildUi(const QStringList& filePaths);

    QPointer<QListWidget> listWidget_;
};
