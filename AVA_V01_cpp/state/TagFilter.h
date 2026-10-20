#pragma once

#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>

/// Drill-down filter from the stats tree. `followUpEvent` empty means every
/// follow-up of `mainEvent`.
struct EventPathFilter {
  QString mainEvent;
  QString followUpEvent;
};

/// Which tags the work table shows.
///
/// Precedence is explicit: when `eventPath` is set, it is the only rule.
/// `hiddenMainEvents` applies only while no event path is set. Checkbox state
/// in the menu is a rendering of this value, not a second copy of it.
struct TagFilter {
  QSet<QString> hiddenMainEvents;
  std::optional<EventPathFilter> eventPath;

  bool allows(const QString& mainEvent, const QString& followUpEvent) const;
  bool isActive() const;

  void showAllMainEvents();
  void hideAllMainEvents(const QStringList& mainEvents);
  void clearEventPath();
};
