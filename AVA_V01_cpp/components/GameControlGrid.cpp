#include "GameControlGrid.h"

#include "EventCatalog.h"

#include <QGridLayout>
#include <QPushButton>
#include <QSizePolicy>

#include <QtGlobal>

namespace GameControlGrid {

std::unique_ptr<QWidget, QtParentDeleter> makeAlignedKeyGrid(
    QWidget* parent, const QList<QList<QPushButton*>>& rows) {
  Q_ASSERT(parent != nullptr);
  auto host = makeQtPtr<QWidget>(parent);
  host->setObjectName(QLatin1String(kObjectName));
  // Minimum, not Fixed: a tagging key's stacked title is taller than the
  // button's size hint. Fixed would cap the grid at that hint and the rows
  // would overlap. Minimum lets the grid grow to the keys' minimum height
  // without taking leftover vertical space.
  host->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
  host->setMinimumWidth(0);

  auto layout = makeQtPtr<QGridLayout>(host.get());
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setHorizontalSpacing(kKeyGapPx);
  layout->setVerticalSpacing(kKeyGapPx);
  for (int column = 0; column < EventCatalog::kButtonGridColumnCount; ++column) {
    layout->setColumnStretch(column, 1);
    layout->setColumnMinimumWidth(column, 0);
  }

  for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
    const QList<QPushButton*>& row = rows.at(rowIndex);
    Q_ASSERT(row.size() <= EventCatalog::kButtonGridColumnCount);
    const int columnCount = qMin(row.size(), EventCatalog::kButtonGridColumnCount);
    for (int columnIndex = 0; columnIndex < columnCount; ++columnIndex) {
      QPushButton* button = row.at(columnIndex);
      if (button == nullptr) continue;
      layout->addWidget(button, rowIndex, columnIndex);
    }
  }

  return host;
}

}  // namespace GameControlGrid
