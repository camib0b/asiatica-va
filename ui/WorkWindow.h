#pragma once

#include <QWidget>
#include <QtGlobal>
#include <QString>
// (state is passed in from MainWindow)
#include <QHash>
#include <QSet>

class QLabel;
class QAction;
class QToolButton;
class QMenu;
class QListWidget;
class QListWidgetItem;

class VideoPlayer;
class GameControls;
class TagSession;
class StatsWindow;

class WorkWindow final : public QWidget {
  Q_OBJECT

public:
  explicit WorkWindow(QWidget* parent = nullptr);
  ~WorkWindow() override = default;

  void loadVideoFromFile(const QString& filePath);
  void setTagSession(TagSession* session);

signals:
  void videoClosed();

private slots:
  void onReplaceVideo();
  void onDiscardVideo();
  void onTagItemActivated(QListWidgetItem* item);
  void onDeleteSelectedTag();
  void onSelectAllFilters();
  void onSelectNoFilters();
  void onFilterActionToggled(bool checked);

private:
  void buildUi();
  void wireSignals();
  void rebuildTagsList();
  void rebuildFilterMenu();
  void updateFilterIndicator();
  bool isMainEventAllowed(const QString& mainEvent) const;

  QString promptForVideoFile();

  // discard or swap video files:
  QToolButton* videoMenuButton_ = nullptr;
  QMenu* videoMenu_ = nullptr;
  QAction* replaceVideoAction_ = nullptr;
  QAction* discardVideoAction_ = nullptr;

  // UI:
  QLabel* headerLabel_ = nullptr;
  VideoPlayer* videoPlayer_ = nullptr;
  GameControls* gameControls_ = nullptr;
  StatsWindow* statsWindow_ = nullptr;

  QLabel* tagsHeaderLabel_ = nullptr;
  QToolButton* tagsFilterButton_ = nullptr;
  QMenu* tagsFilterMenu_ = nullptr;
  QLabel* tagsFilterIndicator_ = nullptr;
  QListWidget* tagsList_ = nullptr;

  TagSession* tagSession_ = nullptr;
  QHash<QString, QAction*> filterActionByMainEvent_;
  QSet<QString> allowedMainEvents_;

  QString pendingMainEvent_;
  qint64 pendingTimestampMs_ = 0;
  bool hasPendingTag_ = false;
};
