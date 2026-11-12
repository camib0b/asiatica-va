#pragma once

#include "../ui/QtPtr.h"

#include <QList>

#include <memory>

class QPushButton;
class QWidget;

namespace GameControlGrid {

/// Gap between tagging keys. The side panel and the layout editor share it.
inline constexpr int kKeyGapPx = 4;

/// Object name of the grid host. Tests look this widget up.
inline constexpr const char kObjectName[] = "GameControlKeyGrid";

/// Straight rows and columns that fill the parent's width.
///
/// Each inner list is one row, left to right. A short row leaves the remaining
/// cells empty, so those keys stay the same width as the rows above. Buttons
/// keep the size policy their caller set. The host is parented to \p parent.
std::unique_ptr<QWidget, QtParentDeleter> makeAlignedKeyGrid(
    QWidget* parent, const QList<QList<QPushButton*>>& rows);

}  // namespace GameControlGrid
