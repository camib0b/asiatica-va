#include "TagRowDisplay.h"

#include "../i18n/AppLocale.h"
#include "../state/TagEvent.h"

namespace TagRowDisplay {

QString formatTimestampMs(qint64 milliseconds) {
  const qint64 clampedMilliseconds = milliseconds < 0 ? 0 : milliseconds;
  const qint64 totalSeconds = clampedMilliseconds / 1000;
  const qint64 hours = totalSeconds / 3600;
  const qint64 minutes = (totalSeconds / 60) % 60;
  const qint64 seconds = totalSeconds % 60;

  if (hours > 0) {
    return QStringLiteral("%1:%2:%3")
        .arg(hours)
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
  }

  const qint64 minutesTotal = totalSeconds / 60;
  return QStringLiteral("%1:%2")
      .arg(minutesTotal, 2, 10, QChar('0'))
      .arg(seconds, 2, 10, QChar('0'));
}

QColor teamBackgroundForTag(const TagSession::GameTag& tag, const TagSession* tagSession) {
  if (!tagSession) return {};
  QString hex;
  if (tag.team == QStringLiteral("Home")) {
    hex = tagSession->homeTeamColor();
  } else if (tag.team == QStringLiteral("Away")) {
    hex = tagSession->awayTeamColor();
  } else {
    return {};
  }
  QString hexClean = hex.trimmed();
  if (hexClean.isEmpty()) return {};
  if (!hexClean.startsWith(QLatin1Char('#'))) {
    hexClean.prepend(QLatin1Char('#'));
  }
  const QColor backgroundColor(hexClean);
  return backgroundColor.isValid() ? backgroundColor : QColor();
}

QString followUpPathForDisplay(const QString& followUpEvent, const TagSession* tagSession) {
  if (!tagSession || followUpEvent.isEmpty()) {
    return followUpEvent;
  }
  return AppLocale::followUpPathWithoutTeamSegments(followUpEvent, tagSession->homeTeamName(),
                                                    tagSession->awayTeamName());
}

QString eventLineForTag(const TagSession::GameTag& tag) {
  return TagEvent::displayTagLine(tag.mainEventKey(), tag.followUpChain());
}

QString displayTeamForTag(const TagSession::GameTag& tag, const TagSession* tagSession) {
  if (!tagSession) {
    if (tag.team == QStringLiteral("Home")) return QStringLiteral("Home");
    if (tag.team == QStringLiteral("Away")) return QStringLiteral("Away");
    return tag.team.isEmpty() ? QStringLiteral("—") : tag.team;
  }
  if (tag.team == QStringLiteral("Home")) {
    const QString teamName = tagSession->homeTeamName();
    return teamName.isEmpty() ? QStringLiteral("Home") : teamName;
  }
  if (tag.team == QStringLiteral("Away")) {
    const QString teamName = tagSession->awayTeamName();
    return teamName.isEmpty() ? QStringLiteral("Away") : teamName;
  }
  return tag.team.isEmpty() ? QStringLiteral("—") : tag.team;
}

}  // namespace TagRowDisplay
