#pragma once

#include "../state/TagSession.h"

#include <QPointer>
#include <QWidget>

class QLabel;
class QTreeWidget;
class QButtonGroup;
class QToolButton;
class QTreeWidgetItem;

class StatsWindow final : public QWidget {
  Q_OBJECT

public:
  explicit StatsWindow(QWidget* parent = nullptr);
  ~StatsWindow() override;

  void setTagSession(TagSession* session);
  void applyUiStrings();

signals:
  void filterByEventPathRequested(const QString& mainEvent, const QString& followUpEvent);

private slots:
  void onTreeItemDoubleClicked(QTreeWidgetItem* item, int column);
  void onSessionCleared();
  void onSessionTagsChanged();
  void onSessionGameMetadataChanged();
  void onTeamFilterClicked(int id);

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

  QPointer<TagSession> tagSession_;
};
