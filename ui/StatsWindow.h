#pragma once

#include <QWidget>

class QLabel;
class QTreeWidget;
class TagSession;

class StatsWindow final : public QWidget {
  Q_OBJECT
  
public:
  explicit StatsWindow(QWidget* parent = nullptr);
  ~StatsWindow() override = default;

  void setTagSession(TagSession* session);

private:
  void buildUi();
  void wireSignals();
  void rebuildTree();
  void clearTree();

  QLabel* headerLabel_ = nullptr;
  QTreeWidget* tree_ = nullptr;

  TagSession* tagSession_ = nullptr;
};
