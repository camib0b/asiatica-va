#pragma once

#include <QObject>
#include <QHash>
#include <QString>
#include <QVector>
#include <QtGlobal>

class TagSession final : public QObject {
  Q_OBJECT

public:
  struct GameTag {
    QString mainEvent;
    QString followUpEvent;
    qint64 positionMs = 0;
    QString note;
    QString period;   // e.g. "Q1", "Q2", "Q3", "Q4"
    QString team;    // e.g. "Home", "Away"
    QString situation; // e.g. "Attacking", "Defending"
  };

  explicit TagSession(QObject* parent = nullptr);
  ~TagSession() override = default;

  void clear();
  void clearTeamInfo();
  void setGameTeams(const QString& homeName, const QString& awayName,
                   const QString& homeColor, const QString& awayColor);
  QString homeTeamName() const { return homeTeamName_; }
  QString awayTeamName() const { return awayTeamName_; }
  QString homeTeamColor() const { return homeTeamColor_; }
  QString awayTeamColor() const { return awayTeamColor_; }

  void addTag(const GameTag& tag);
  void removeTag(int index);
  void setTagNote(int index, const QString& note);
  QString tagNote(int index) const;

  const QVector<GameTag>& tags() const { return tags_; }
  const QHash<QString, int>& mainEventCounts() const { return mainEventCounts_; }
  const QHash<QString, QHash<QString, int>>& followUpCountsByMainEvent() const { return followUpCountsByMainEvent_; }

signals:
  void cleared();
  void tagAdded(const TagSession::GameTag& tag);
  void tagNoteChanged(int index);
  void statsChanged();

private:
  QVector<GameTag> tags_;
  QHash<QString, int> mainEventCounts_;
  QHash<QString, QHash<QString, int>> followUpCountsByMainEvent_;
  QString homeTeamName_;
  QString awayTeamName_;
  QString homeTeamColor_;
  QString awayTeamColor_;
};

