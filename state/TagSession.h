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
  };

  explicit TagSession(QObject* parent = nullptr);
  ~TagSession() override = default;

  void clear();
  void addTag(const GameTag& tag);

  const QVector<GameTag>& tags() const { return tags_; }
  const QHash<QString, int>& mainEventCounts() const { return mainEventCounts_; }
  const QHash<QString, QHash<QString, int>>& followUpCountsByMainEvent() const { return followUpCountsByMainEvent_; }

signals:
  void cleared();
  void tagAdded(const TagSession::GameTag& tag);
  void statsChanged();

private:
  QVector<GameTag> tags_;
  QHash<QString, int> mainEventCounts_;
  QHash<QString, QHash<QString, int>> followUpCountsByMainEvent_;
};

