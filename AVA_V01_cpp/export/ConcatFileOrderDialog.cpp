#include "ConcatFileOrderDialog.h"

#include "../i18n/AppLocale.h"
#include "../style/StyleProps.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>

ConcatFileOrderDialog::ConcatFileOrderDialog(const QStringList& filePaths, QWidget* parent)
    : QDialog(parent),
      listWidget_(nullptr) {
    buildUi(filePaths);
}

QStringList ConcatFileOrderDialog::orderedFilePaths() const {
    QStringList orderedPaths;
    if (!listWidget_) return orderedPaths;
    orderedPaths.reserve(listWidget_->count());
    for (int i = 0; i < listWidget_->count(); ++i) {
        const QListWidgetItem* item = listWidget_->item(i);
        if (!item) continue;
        orderedPaths.append(item->data(Qt::UserRole).toString());
    }
    return orderedPaths;
}

void ConcatFileOrderDialog::moveCurrentItemLeft() {
    if (!listWidget_) return;
    const int row = listWidget_->currentRow();
    if (row <= 0) return;
    QListWidgetItem* item = listWidget_->takeItem(row);
    if (!item) return;
    listWidget_->insertItem(row - 1, item);
    listWidget_->setCurrentRow(row - 1);
}

void ConcatFileOrderDialog::moveCurrentItemRight() {
    if (!listWidget_) return;
    const int row = listWidget_->currentRow();
    if (row < 0 || row >= listWidget_->count() - 1) return;
    QListWidgetItem* item = listWidget_->takeItem(row);
    if (!item) return;
    listWidget_->insertItem(row + 1, item);
    listWidget_->setCurrentRow(row + 1);
}

void ConcatFileOrderDialog::buildUi(const QStringList& filePaths) {
    setWindowTitle(AppLocale::trUi("concat.dialog_title"));
    Style::setRole(this, "fileOrder");

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(16);
    layout->setContentsMargins(24, 24, 24, 24);

    auto* titleLabel = new QLabel(AppLocale::trUi("concat.dialog_title"), this);
    Style::setRole(titleLabel, "h1");
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);

    auto* listWidget = new QListWidget(this);
    Style::setRole(listWidget, "chipStrip");
    listWidget->setFrameShape(QFrame::NoFrame);
    listWidget->setFlow(QListView::LeftToRight);
    listWidget->setWrapping(true);
    listWidget->setResizeMode(QListView::Adjust);
    listWidget->setSpacing(6);
    listWidget->setDragDropMode(QAbstractItemView::InternalMove);
    listWidget->setDefaultDropAction(Qt::MoveAction);
    listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    listWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    listWidget->setTextElideMode(Qt::ElideMiddle);

    for (const QString& path : filePaths) {
        auto* item = new QListWidgetItem(QFileInfo(path).fileName());
        item->setData(Qt::UserRole, path);
        item->setTextAlignment(Qt::AlignCenter);
        listWidget->addItem(item);
    }
    if (listWidget->count() > 0) listWidget->setCurrentRow(0);
    layout->addWidget(listWidget);
    listWidget_ = listWidget;

    auto* moveRow = new QHBoxLayout();
    moveRow->setSpacing(8);
    auto* moveLeftButton = new QPushButton(AppLocale::trUi("concat.move_left"), this);
    auto* moveRightButton = new QPushButton(AppLocale::trUi("concat.move_right"), this);
    moveLeftButton->setCursor(Qt::PointingHandCursor);
    moveRightButton->setCursor(Qt::PointingHandCursor);
    Style::setVariant(moveLeftButton, "ghost");
    Style::setSize(moveLeftButton, "sm");
    Style::setVariant(moveRightButton, "ghost");
    Style::setSize(moveRightButton, "sm");
    moveRow->addStretch(1);
    moveRow->addWidget(moveLeftButton);
    moveRow->addWidget(moveRightButton);
    moveRow->addStretch(1);
    layout->addLayout(moveRow);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(12);
    auto* cancelButton = new QPushButton(AppLocale::trUi("concat.cancel"), this);
    auto* continueButton = new QPushButton(AppLocale::trUi("concat.continue_btn"), this);
    cancelButton->setCursor(Qt::PointingHandCursor);
    continueButton->setCursor(Qt::PointingHandCursor);
    Style::setVariant(cancelButton, "ghost");
    Style::setSize(cancelButton, "md");
    Style::setVariant(continueButton, "welcomeImport");
    Style::setSize(continueButton, "lg");
    buttonRow->addStretch(1);
    buttonRow->addWidget(cancelButton);
    buttonRow->addWidget(continueButton);
    buttonRow->addStretch(1);
    layout->addLayout(buttonRow);

    connect(moveLeftButton, &QPushButton::clicked, this, &ConcatFileOrderDialog::moveCurrentItemLeft);
    connect(moveRightButton, &QPushButton::clicked, this, &ConcatFileOrderDialog::moveCurrentItemRight);
    connect(continueButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
}
