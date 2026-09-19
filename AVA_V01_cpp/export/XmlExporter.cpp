#include "XmlExporter.h"

#include "EventCodeMap.h"
#include "EventDefaults.h"
#include "TagSession.h"

#include <QColor>
#include <QFileDevice>
#include <QHash>
#include <QSaveFile>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QXmlStreamWriter>

#include <algorithm>
#include <optional>

namespace XmlExporter {

namespace {

/// Time-control codes are written verbatim as the <code>: Q1, Q2, Q3, Q4, Inicio, TM.
QString neutralPassThroughCode(const QString& canonicalMainEvent) {
  if (EventDefaults::isTimeControlEvent(canonicalMainEvent)) {
    return canonicalMainEvent;
  }
  return QString();
}

QString secondsString(qint64 ms) {
  const qint64 wholeSeconds = ms / 1000;
  const qint64 millisPart = ms % 1000;
  if (millisPart == 0) {
    return QString::number(wholeSeconds);
  }
  int precision = 3;
  if (millisPart % 10 == 0) {
    precision = 2;
  }
  if (millisPart % 100 == 0) {
    precision = 1;
  }
  const double seconds = static_cast<double>(ms) / 1000.0;
  return QString::number(seconds, 'f', precision);
}

/// Keeps only characters allowed in XML 1.0 text nodes. QXmlStreamWriter already escapes
/// &, <, and > via writeCharacters(), but invalid control characters make the writer fail.
QString xmlTextContent(const QString& raw) {
  QString sanitized;
  sanitized.reserve(raw.size());
  for (int index = 0; index < raw.size(); ++index) {
    const QChar character = raw.at(index);
    if (character.isHighSurrogate() && index + 1 < raw.size() &&
        raw.at(index + 1).isLowSurrogate()) {
      sanitized.append(character);
      sanitized.append(raw.at(index + 1));
      ++index;
      continue;
    }
    if (character.isSurrogate()) continue;

    const ushort code = character.unicode();
    const bool allowed = code == 0x9 || code == 0xA || code == 0xD ||
                         (code >= 0x20 && code <= 0xD7FF) ||
                         (code >= 0xE000 && code <= 0xFFFD);
    if (allowed) sanitized.append(character);
  }
  return sanitized;
}

void writeXmlTextElement(QXmlStreamWriter& writer,
                         const QString& elementName,
                         const QString& text) {
  writer.writeTextElement(elementName, xmlTextContent(text));
}

/// Clip interval written to <start>/<end>: current EventDefaults lead/lag around \p tag.markMs,
/// unless the tag was manually trimmed or is a game-time span (quarters, Inicio, TM).
QPair<qint64, qint64> exportIntervalFor(const TagSession::GameTag& tag) {
  auto clampInterval = [](qint64 startMs, qint64 endMs) {
    if (startMs < 0) startMs = 0;
    if (endMs < startMs) endMs = startMs;
    return QPair<qint64, qint64>{startMs, endMs};
  };

  if (tag.intervalManuallyEdited) {
    return clampInterval(tag.startMs, tag.endMs);
  }

  if (EventDefaults::isTimeControlEvent(tag.mainEvent)) {
    return clampInterval(tag.startMs, tag.endMs);
  }

  const EventDefaults::EventDuration duration = EventDefaults::defaultFor(tag.mainEvent);
  qint64 startMs = tag.markMs - duration.leadMs;
  qint64 endMs = tag.markMs + duration.lagMs;
  return clampInterval(startMs, endMs);
}

QColor parseHexColor(const QString& hex, const QColor& fallback) {
  if (hex.trimmed().isEmpty()) return fallback;
  QString cleaned = hex.trimmed();
  if (!cleaned.startsWith(QLatin1Char('#'))) cleaned.prepend(QLatin1Char('#'));
  const QColor c(cleaned);
  return c.isValid() ? c : fallback;
}

/// Home/away identity used for instance codes, RESULTADO text, and <ROWS> colors.
struct TeamInfo {
  QString homeAbbrev;
  QString awayAbbrev;
  QString homeName;
  QString awayName;
  QColor homeColor;
  QColor awayColor;

  bool hasBothAbbrevs() const {
    return !homeAbbrev.isEmpty() && !awayAbbrev.isEmpty();
  }

  QString abbrevForTeam(const QString& team) const {
    if (team == QStringLiteral("Home")) return homeAbbrev;
    if (team == QStringLiteral("Away")) return awayAbbrev;
    return QString();
  }

  QString opposingAbbrevForTeam(const QString& team) const {
    if (team == QStringLiteral("Home")) return awayAbbrev;
    if (team == QStringLiteral("Away")) return homeAbbrev;
    return QString();
  }

  QString homeScoreboardLabel() const {
    return homeAbbrev.isEmpty() ? homeName : homeAbbrev;
  }

  QString awayScoreboardLabel() const {
    return awayAbbrev.isEmpty() ? awayName : awayAbbrev;
  }
};

TeamInfo teamInfoFromSession(const TagSession& session) {
  TeamInfo teams;
  teams.homeAbbrev = session.homeAbbrev();
  teams.awayAbbrev = session.awayAbbrev();
  teams.homeName = session.homeTeamName();
  teams.awayName = session.awayTeamName();
  teams.homeColor = parseHexColor(session.homeTeamColor(), QColor(60, 90, 200));
  teams.awayColor = parseHexColor(session.awayTeamColor(), QColor(200, 60, 60));
  return teams;
}

/// Converts an 8-bit channel (0..255) to a 16-bit channel (0..65535) in the same way the
/// reference XML does (every 8-bit value maps to value * 257 so 0xff -> 0xffff).
int eightBitToSixteenBit(int value8) {
  if (value8 < 0) value8 = 0;
  if (value8 > 255) value8 = 255;
  return value8 * 257;
}

/// Result of mapping a tagged GameTag to one or more concrete <instance> entries.
struct EmittedInstance {
  qint64 startMs;
  qint64 endMs;
  QString code;
  QString period; // empty = no QUARTOS label
  bool includeMatchLabels = true;
};

/// True when a follow-up path records a scored goal (segment "Goal", not "No Goal").
bool followUpPathContainsScoredGoal(const QString& followUpEvent) {
  if (followUpEvent.trimmed().isEmpty()) return false;
  const QStringList segments =
      followUpEvent.split(QStringLiteral(" → "), Qt::KeepEmptyParts);
  for (const QString& segment : segments) {
    if (segment.trimmed() == QStringLiteral("Goal")) return true;
  }
  return false;
}

/// Clip span used to decide whether an explicit Goal belongs to \p tag. Manually trimmed
/// tags keep their export interval for <start>/<end>, but goal association must stay near
/// markMs so a wide trim does not treat every Goal in the clip as confirmation.
QPair<qint64, qint64> goalAssociationIntervalFor(const TagSession::GameTag& tag) {
  if (!tag.intervalManuallyEdited) {
    return exportIntervalFor(tag);
  }

  const EventDefaults::EventDuration duration = EventDefaults::defaultFor(tag.mainEvent);
  qint64 startMs = tag.markMs - duration.leadMs;
  qint64 endMs = tag.markMs + duration.lagMs;
  if (startMs < 0) startMs = 0;
  if (endMs < startMs) endMs = startMs;
  return {startMs, endMs};
}

/// Precomputed explicit Goal used to decide whether a follow-up already has a tagged Goal.
/// Intervals are cached so each Goal is measured once, not once per candidate source.
struct ExplicitGoal {
  QString team;
  qint64 markMs = 0;
  qint64 associationStartMs = 0;
  qint64 associationEndMs = 0;
};

QVector<ExplicitGoal> collectExplicitGoals(const QVector<TagSession::GameTag>& tags) {
  QVector<ExplicitGoal> goals;
  for (const auto& tag : tags) {
    if (tag.mainEvent != QStringLiteral("Goal")) continue;
    const QPair<qint64, qint64> interval = goalAssociationIntervalFor(tag);
    ExplicitGoal goal;
    goal.team = tag.team;
    goal.markMs = tag.markMs;
    goal.associationStartMs = interval.first;
    goal.associationEndMs = interval.second;
    goals.append(goal);
  }
  return goals;
}

/// True when the session already has an explicit Goal tag for the same team tied to \p source
/// (overlapping default clip around markMs or goal confirmation shortly after the originating tag).
bool hasExplicitGoalTagForTeam(const QVector<ExplicitGoal>& explicitGoals,
                               const TagSession::GameTag& source) {
  if (source.team.isEmpty()) return false;
  const QPair<qint64, qint64> sourceInterval = goalAssociationIntervalFor(source);
  constexpr qint64 kGoalConfirmWindowAfterOriginMs = 60000;
  for (const ExplicitGoal& goal : explicitGoals) {
    if (goal.team != source.team) continue;
    if (goal.associationStartMs <= sourceInterval.second &&
        goal.associationEndMs >= sourceInterval.first) {
      return true;
    }
    if (goal.markMs >= source.markMs &&
        goal.markMs - source.markMs <= kGoalConfirmWindowAfterOriginMs) {
      return true;
    }
  }
  return false;
}

/// Emission order: game time (markMs), originating event before a Goal at the same mark,
/// then exported clip start. Clip lead/lag must not pull a derived Goal ahead of its origin
/// (Goal defaults are a longer lead than Shot/PC).
bool exportTagComesBefore(const TagSession::GameTag& a, const TagSession::GameTag& b) {
  if (a.markMs != b.markMs) return a.markMs < b.markMs;
  const bool aIsGoal = a.mainEvent == QStringLiteral("Goal");
  const bool bIsGoal = b.mainEvent == QStringLiteral("Goal");
  if (aIsGoal != bIsGoal) return bIsGoal;
  if (a.startMs != b.startMs) return a.startMs < b.startMs;
  return false;
}

/// Adds synthetic Goal tags when a scored goal is implied by follow-up but never tagged as Goal.
QVector<TagSession::GameTag> tagsForExport(const QVector<TagSession::GameTag>& tags) {
  // Copy is required: we stamp export intervals onto a working list and append synthetics.
  // Session order is startMs/markMs, not the emission comparator, so a linear merge cannot
  // replace the later stable_sort.
  const QVector<ExplicitGoal> explicitGoals = collectExplicitGoals(tags);
  QVector<TagSession::GameTag> expanded = tags;
  expanded.reserve(tags.size() + 8);
  for (const auto& tag : tags) {
    if (tag.mainEvent == QStringLiteral("Goal")) continue;
    if (!followUpPathContainsScoredGoal(tag.followUpEvent)) continue;
    if (hasExplicitGoalTagForTeam(explicitGoals, tag)) continue;

    TagSession::GameTag goalTag = tag;
    goalTag.mainEvent = QStringLiteral("Goal");
    goalTag.followUpEvent.clear();
    // Synthetic Goals always use Goal lead/lag from EventDefaults around markMs.
    // Never inherit the originating tag's intervalManuallyEdited flag or trimmed span.
    goalTag.intervalManuallyEdited = false;
    expanded.append(goalTag);
  }

  // Stamp the interval that will be written to <start>/<end> so the sort's startMs
  // key matches emission. Derived Goals get Goal defaults even when the origin was trimmed.
  for (TagSession::GameTag& tag : expanded) {
    const QPair<qint64, qint64> interval = exportIntervalFor(tag);
    tag.startMs = interval.first;
    tag.endMs = interval.second;
  }

  std::stable_sort(expanded.begin(), expanded.end(), exportTagComesBefore);
  return expanded;
}

/// Running score from Goal instances in \p tags with emission index <= \p throughIndex.
/// \p tags must already be in emission order (see tagsForExport). This is index-based,
/// not markMs-based, so an originating event at markMs T is labeled before a derived Goal
/// at the same mark (exportTagComesBefore) even though both share T.
QPair<int, int> runningScoreAt(const QVector<TagSession::GameTag>& tags, int throughIndex) {
  int home = 0;
  int away = 0;
  for (int index = 0; index <= throughIndex && index < tags.size(); ++index) {
    const TagSession::GameTag& tag = tags.at(index);
    if (tag.mainEvent != QStringLiteral("Goal")) continue;
    if (tag.team == QStringLiteral("Home")) ++home;
    else if (tag.team == QStringLiteral("Away")) ++away;
  }
  return {home, away};
}

/// Turns a single GameTag into its zero, one, or two emitted XML instances.
QVector<EmittedInstance> emittedInstancesFor(const TagSession::GameTag& tag,
                                             const TeamInfo& teams) {
  QVector<EmittedInstance> result;
  const QPair<qint64, qint64> interval = exportIntervalFor(tag);
  const qint64 exportStartMs = interval.first;
  const qint64 exportEndMs = interval.second;

  // Neutral / pass-through code (Q1..Q4, Inicio, TM): one <instance>, no team affiliation.
  const QString neutralCode = neutralPassThroughCode(tag.mainEvent);
  if (!neutralCode.isEmpty()) {
    EmittedInstance instance;
    instance.startMs = exportStartMs;
    instance.endMs = exportEndMs;
    instance.code = neutralCode;
    instance.period = tag.period;
    // Quarter / start-anchor instances do not carry the per-event metadata labels in the
    // reference (see <ID>1 Q1, <ID>3 Inicio, <ID>13 TM examples), so suppress them.
    instance.includeMatchLabels = false;
    result.append(instance);
    return result;
  }

  // Team-affiliated code: requires both team abbreviation and a short code mapping. When
  // either is missing we still emit a single neutral <code> using the canonical event name
  // so the user does not silently lose information.
  const std::optional<QString> shortCode = EventCodeMap::shortCodeForMainEvent(tag.mainEvent);
  const QString taggedAbbrev = teams.abbrevForTeam(tag.team);
  const QString opposingAbbrev = teams.opposingAbbrevForTeam(tag.team);

  if (!shortCode.has_value() || !teams.hasBothAbbrevs() || taggedAbbrev.isEmpty()) {
    EmittedInstance instance;
    instance.startMs = exportStartMs;
    instance.endMs = exportEndMs;
    instance.code = tag.mainEvent;
    instance.period = tag.period;
    result.append(instance);
    return result;
  }

  EmittedInstance positive;
  positive.startMs = exportStartMs;
  positive.endMs = exportEndMs;
  positive.code = QStringLiteral("%1 %2+").arg(taggedAbbrev, *shortCode);
  positive.period = tag.period;
  result.append(positive);

  EmittedInstance negative;
  negative.startMs = exportStartMs;
  negative.endMs = exportEndMs;
  negative.code = QStringLiteral("%1 %2-").arg(opposingAbbrev, *shortCode);
  negative.period = tag.period;
  result.append(negative);

  return result;
}

QString resultadoLabelFor(const TeamInfo& teams, const QPair<int, int>& score) {
  if (teams.homeAbbrev.isEmpty() && teams.awayAbbrev.isEmpty()) return QString();
  return QStringLiteral("%1 %2 - %3 %4")
      .arg(teams.homeScoreboardLabel())
      .arg(score.first)
      .arg(score.second)
      .arg(teams.awayScoreboardLabel());
}

/// RGB triple in 16-bit Olympia/LongoMatch format.
struct Rgb16 {
  int r = 0;
  int g = 0;
  int b = 0;
};

Rgb16 colorForCode(const QString& code, const TeamInfo& teams) {
  // Quarter palette (deterministic and visually distinguishable; values picked to keep
  // sufficient contrast between adjacent quarters).
  static const QHash<QString, QColor> kQuarterPalette = {
      {QStringLiteral("Q1"), QColor(80, 130, 180)},   // steel blue
      {QStringLiteral("Q2"), QColor(110, 160, 110)},  // sage green
      {QStringLiteral("Q3"), QColor(190, 150, 80)},   // dusty gold
      {QStringLiteral("Q4"), QColor(170, 110, 150)},  // mauve
  };
  if (kQuarterPalette.contains(code)) {
    const QColor c = kQuarterPalette.value(code);
    return {eightBitToSixteenBit(c.red()),
            eightBitToSixteenBit(c.green()),
            eightBitToSixteenBit(c.blue())};
  }

  auto colorFromTeam = [](const QColor& teamColor) {
    return Rgb16{eightBitToSixteenBit(teamColor.red()),
                 eightBitToSixteenBit(teamColor.green()),
                 eightBitToSixteenBit(teamColor.blue())};
  };

  if (!teams.homeAbbrev.isEmpty() &&
      code.startsWith(teams.homeAbbrev + QLatin1Char(' '))) {
    return colorFromTeam(teams.homeColor);
  }
  if (!teams.awayAbbrev.isEmpty() &&
      code.startsWith(teams.awayAbbrev + QLatin1Char(' '))) {
    return colorFromTeam(teams.awayColor);
  }

  // Neutral mid-gray for everything else (Inicio, TM, untagged events).
  return {eightBitToSixteenBit(150), eightBitToSixteenBit(150), eightBitToSixteenBit(150)};
}

} // namespace

bool writeAllInstances(const TagSession* session,
                       const QString& filePath,
                       QString* errorMessage) {
  if (!session) {
    if (errorMessage) *errorMessage = QStringLiteral("No session to export.");
    return false;
  }
  if (filePath.trimmed().isEmpty()) {
    if (errorMessage) *errorMessage = QStringLiteral("No output file path provided.");
    return false;
  }

  // Expand follow-up Goals, then sort that full list so instance IDs and RESULTADO
  // walk game time (synthetic Goals are not visible to a pre-sort of session tags).
  const QVector<TagSession::GameTag> exportTags = tagsForExport(session->tags());

  const TeamInfo teams = teamInfoFromSession(*session);
  const QString competitionName = session->competitionName();
  const int gameYear = session->gameYear();

  QSaveFile file(filePath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    if (errorMessage) {
      *errorMessage = QStringLiteral("Failed to open file for writing: ") + file.errorString();
    }
    return false;
  }

  QXmlStreamWriter writer(&file);
  writer.setAutoFormatting(true);
  writer.setAutoFormattingIndent(2);
  writer.writeStartDocument();
  writer.writeStartElement(QStringLiteral("file"));

  // ---- <ALL_INSTANCES> ----
  writer.writeStartElement(QStringLiteral("ALL_INSTANCES"));

  // Unique codes in first-seen timeline order for the <ROWS> palette.
  QStringList emittedCodesOrder;

  int nextInstanceId = 1;
  for (int tagIndex = 0; tagIndex < exportTags.size(); ++tagIndex) {
    const TagSession::GameTag& tag = exportTags.at(tagIndex);
    const QVector<EmittedInstance> instances = emittedInstancesFor(tag, teams);
    if (instances.isEmpty()) continue;

    const QPair<int, int> score = runningScoreAt(exportTags, tagIndex);
    const QString resultadoLabel = resultadoLabelFor(teams, score);

    for (const auto& instance : instances) {
      writer.writeStartElement(QStringLiteral("instance"));
      writer.writeTextElement(QStringLiteral("ID"), QString::number(nextInstanceId++));
      writer.writeTextElement(QStringLiteral("start"), secondsString(instance.startMs));
      writer.writeTextElement(QStringLiteral("end"), secondsString(instance.endMs));
      writeXmlTextElement(writer, QStringLiteral("code"), instance.code);

      if (!emittedCodesOrder.contains(instance.code)) {
        emittedCodesOrder.append(instance.code);
      }

      if (instance.includeMatchLabels) {
        if (!competitionName.isEmpty()) {
          writer.writeStartElement(QStringLiteral("label"));
          writer.writeTextElement(QStringLiteral("group"), QStringLiteral("COMPETICION"));
          writeXmlTextElement(writer, QStringLiteral("text"), competitionName);
          writer.writeEndElement();
        }
        if (!resultadoLabel.isEmpty()) {
          writer.writeStartElement(QStringLiteral("label"));
          writer.writeTextElement(QStringLiteral("group"), QStringLiteral("RESULTADO"));
          writeXmlTextElement(writer, QStringLiteral("text"), resultadoLabel);
          writer.writeEndElement();
        }
        if (!instance.period.isEmpty()) {
          writer.writeStartElement(QStringLiteral("label"));
          writer.writeTextElement(QStringLiteral("group"), QStringLiteral("QUARTOS"));
          writeXmlTextElement(writer, QStringLiteral("text"), instance.period);
          writer.writeEndElement();
        }
        if (gameYear > 0) {
          writer.writeStartElement(QStringLiteral("label"));
          writer.writeTextElement(QStringLiteral("group"), QStringLiteral("ANO"));
          writer.writeTextElement(QStringLiteral("text"), QString::number(gameYear));
          writer.writeEndElement();
        }
      }

      writer.writeEndElement(); // instance
    }
  }

  writer.writeEndElement(); // ALL_INSTANCES

  // ---- <ROWS> ----
  writer.writeStartElement(QStringLiteral("ROWS"));
  for (const QString& code : emittedCodesOrder) {
    const Rgb16 rgb = colorForCode(code, teams);
    writer.writeStartElement(QStringLiteral("row"));
    writeXmlTextElement(writer, QStringLiteral("code"), code);
    writer.writeTextElement(QStringLiteral("R"), QString::number(rgb.r));
    writer.writeTextElement(QStringLiteral("G"), QString::number(rgb.g));
    writer.writeTextElement(QStringLiteral("B"), QString::number(rgb.b));
    writer.writeEndElement(); // row
  }
  writer.writeEndElement(); // ROWS

  writer.writeEndElement(); // file
  writer.writeEndDocument();

  // QXmlStreamWriter::hasError() does not report QIODevice failures. QSaveFile
  // discards the temporary on destruction only if commit() was never called, so
  // cancel any incomplete write while the file is still open.
  const bool xmlWriterFailed = writer.hasError();
  const bool deviceWriteFailed = !file.flush() || file.error() != QFileDevice::NoError;
  if (xmlWriterFailed || deviceWriteFailed) {
    const QString deviceError = file.errorString();
    file.cancelWriting();
    if (errorMessage) {
      if (xmlWriterFailed) {
        *errorMessage = QStringLiteral("XML writer reported an error.");
      } else {
        *errorMessage = QStringLiteral("Failed to write file: ") + deviceError;
      }
    }
    return false;
  }

  if (!file.commit()) {
    if (errorMessage) {
      *errorMessage = QStringLiteral("Failed to commit file: ") + file.errorString();
    }
    return false;
  }

  return true;
}

} // namespace XmlExporter
