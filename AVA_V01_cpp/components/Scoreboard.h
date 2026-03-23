#pragma once

#include <QWidget>
#include <QtGlobal>

class QLabel;
class TagSession;

class Scoreboard final : public QWidget {
  Q_OBJECT

public:
  explicit Scoreboard(QWidget* parent = nullptr);

  void setTagSession(TagSession* session);
  void setCurrentTimestampMs(qint64 positionMs);

private:
  void buildUi();
  void updateScores();
  void updateTeamDisplay();
  int countGoalsForTeam(const QString& teamKey) const;

  TagSession* tagSession_ = nullptr;
  qint64 currentTimestampMs_ = 0;

  QWidget* homeColorSwatch_ = nullptr;
  QLabel* homeTeamNameLabel_ = nullptr;
  QLabel* homeScoreLabel_ = nullptr;
  QLabel* separatorLabel_ = nullptr;
  QLabel* awayScoreLabel_ = nullptr;
  QLabel* awayTeamNameLabel_ = nullptr;
  QWidget* awayColorSwatch_ = nullptr;
};
