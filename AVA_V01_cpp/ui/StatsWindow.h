#pragma once

#include <QWidget>

#include "../state/TagSession.h"

class QLabel;
class QTreeWidget;
class QButtonGroup;
class QToolButton;
class QWidget;
class TagSession;

class StatsWindow final : public QWidget {
  Q_OBJECT
  
public:
  explicit StatsWindow(QWidget* parent = nullptr);
  ~StatsWindow() override = default;

  void setTagSession(TagSession* session);
  void applyUiStrings();

signals:
  void filterByPathRequested(const QString& mainEvent, const QString& followUpEvent);

private slots:
  void onTreeItemDoubleClicked(class QTreeWidgetItem* item, int column);

private:
  enum class TeamStatsFilter : int { Home = 0, Away = 1, Both = 2 };

  void buildUi();
  void wireSignals();
  void rebuildTree();
  void clearTree();
  void updateTeamFilterButtonLabels();
  TeamStatsFilter currentTeamFilter() const;
  bool tagMatchesTeamFilter(const TagSession::GameTag& tag, TeamStatsFilter filter) const;

  QLabel* headerLabel_ = nullptr;
  QWidget* teamFilterRow_ = nullptr;
  QButtonGroup* teamFilterGroup_ = nullptr;
  QToolButton* teamFilterHomeBtn_ = nullptr;
  QToolButton* teamFilterAwayBtn_ = nullptr;
  QToolButton* teamFilterBothBtn_ = nullptr;
  QTreeWidget* tree_ = nullptr;

  TagSession* tagSession_ = nullptr;
};
