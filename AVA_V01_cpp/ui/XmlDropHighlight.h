#pragma once

#include <QColor>
#include <QWidget>

/// Perimeter stroke shown while an XML file is dragged onto an empty session.
/// The stroke sits in the work window's outer padding, on the app surface
/// (#FFFFFF) or the export-jobs bar (#fafafa), and the center stays clear.
class XmlDropHighlight final : public QWidget {
 public:
  explicit XmlDropHighlight(QWidget* parent = nullptr);

  /// Inset from the window edge. Together with strokePx this stays inside
  /// WorkWindow::kContentMarginPx on the top and sides.
  static constexpr int edgeInsetPx = 4;
  static constexpr int strokePx = 3;

  static QColor strokeColor();

 protected:
  void paintEvent(QPaintEvent* event) override;
};
