#pragma once

#include "StyleProps.h"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTableView>
#include <QTreeView>

#include <algorithm>

namespace Style {

/// Pixel height of one data row after the work-list stylesheet has been applied.
/// Uses the polished font and item padding so tagging, analysis, and presentation
/// stay on the same metric instead of copying constants out of theme_light.qss.
inline int workDataViewRowHeight(const QAbstractItemView* view) {
  if (!view) return 0;

  view->ensurePolished();

  QStyleOptionViewItem option;
  option.initFrom(view);
  option.font = view->font();
  option.fontMetrics = view->fontMetrics();
  option.text = QStringLiteral("Đặng Thị Ngọc Hân");
  option.displayAlignment = Qt::AlignLeft | Qt::AlignVCenter;

  const QFontMetrics metrics = view->fontMetrics();
  const int textHeight = std::max(metrics.lineSpacing(),
                                   metrics.boundingRect(option.text).height());
  const QSize contents = view->style()->sizeFromContents(
      QStyle::CT_ItemViewItem, &option, QSize(0, textHeight), view);
  return std::max(contents.height(), textHeight);
}

/// Shared chrome for the tagging event log, the analysis stats tree, and the
/// presentation clip list. Look (color, type, padding, hover, selection, border)
/// lives in theme_light.qss on QAbstractItemView[role="workList"]. Column resize
/// modes stay with the caller because the three lists do not share a column layout.
inline void configureWorkDataView(QAbstractItemView* view) {
  if (!view) return;

  setRole(view, "workList");
  view->setAlternatingRowColors(true);
  view->setSelectionBehavior(QAbstractItemView::SelectRows);
  view->setSelectionMode(QAbstractItemView::SingleSelection);
  view->setEditTriggers(QAbstractItemView::NoEditTriggers);
  view->setTextElideMode(Qt::ElideRight);
  view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  view->setAttribute(Qt::WA_StyledBackground, true);

  QHeaderView* horizontalHeader = nullptr;
  if (auto* table = qobject_cast<QTableView*>(view)) {
    table->setWordWrap(false);
    table->setShowGrid(false);
    table->setCornerButtonEnabled(false);
    if (QHeaderView* verticalHeader = table->verticalHeader()) {
      verticalHeader->hide();
      verticalHeader->setSectionResizeMode(QHeaderView::Fixed);
      verticalHeader->setDefaultSectionSize(workDataViewRowHeight(table));
    }
    horizontalHeader = table->horizontalHeader();
  } else if (auto* tree = qobject_cast<QTreeView*>(view)) {
    tree->setWordWrap(false);
    tree->setUniformRowHeights(true);
    horizontalHeader = tree->header();
  }

  if (!horizontalHeader) return;
  horizontalHeader->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  horizontalHeader->setHighlightSections(false);
}

} // namespace Style
