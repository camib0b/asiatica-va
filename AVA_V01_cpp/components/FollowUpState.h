#pragma once

#include <QString>
#include <QStringList>

/// Position in a main-event follow-up walk. Commit/cancel go through
/// \c takeCommit() / \c reset() so callers cannot leave stale selections behind.
class FollowUpState {
public:
  enum class Stage {
    None,
    FirstLevel,
    SecondLevel,
    ThirdLevel,
  };

  struct Snapshot {
    QString mainEvent;
    QStringList selections;
  };

  void beginMainEvent(const QString& mainEvent);
  void select(const QString& choice);
  void setStage(Stage stage);
  void reset();

  /// Copies the current walk, then resets to idle. Use this as the only
  /// commit/cancel path so UI teardown always sees a clean state.
  Snapshot takeCommit();

  bool isIdle() const;
  const QString& mainEvent() const;
  const QStringList& selections() const;
  Stage stage() const;
  Stage stageAfterSelection() const;

private:
  QString mainEvent_;
  QStringList selections_;
  Stage stage_ = Stage::None;
};

inline void FollowUpState::beginMainEvent(const QString& mainEvent) {
  reset();
  mainEvent_ = mainEvent;
}

inline void FollowUpState::select(const QString& choice) {
  selections_.append(choice);
}

inline void FollowUpState::setStage(Stage stage) {
  stage_ = stage;
}

inline void FollowUpState::reset() {
  mainEvent_.clear();
  selections_.clear();
  stage_ = Stage::None;
}

inline FollowUpState::Snapshot FollowUpState::takeCommit() {
  Snapshot snapshot{mainEvent_, selections_};
  reset();
  return snapshot;
}

inline bool FollowUpState::isIdle() const {
  return stage_ == Stage::None || mainEvent_.isEmpty();
}

inline const QString& FollowUpState::mainEvent() const {
  return mainEvent_;
}

inline const QStringList& FollowUpState::selections() const {
  return selections_;
}

inline FollowUpState::Stage FollowUpState::stage() const {
  return stage_;
}

inline FollowUpState::Stage FollowUpState::stageAfterSelection() const {
  switch (stage_) {
    case Stage::FirstLevel:
      return Stage::SecondLevel;
    case Stage::SecondLevel:
      return Stage::ThirdLevel;
    case Stage::None:
    case Stage::ThirdLevel:
      return Stage::None;
  }
  return Stage::None;
}
