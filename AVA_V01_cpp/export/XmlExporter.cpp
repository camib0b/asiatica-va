#include "XmlExporter.h"

#include "EventCodeMap.h"
#include "EventDefaults.h"
#include "TagSession.h"

#include <QColor>
#include <QFile>
#include <QHash>
#include <QSaveFile>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QXmlStreamWriter>

#include <algorithm>

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
  // The reference XML uses very high precision; we keep enough fractional digits to
  // represent millisecond inputs without loss while staying under 16 chars total.
  const double seconds = static_cast<double>(ms) / 1000.0;
  return QString::number(seconds, 'f', 3);
}

/// Clip interval written to <start>/<end>: current EventDefaults lead/lag around \p positionMs,
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
  qint64 startMs = tag.positionMs - duration.preMs;
  qint64 endMs = tag.positionMs + duration.postMs;
  return clampInterval(startMs, endMs);
}

QColor parseHexColor(const QString& hex, const QColor& fallback) {
  if (hex.trimmed().isEmpty()) return fallback;
  QString cleaned = hex.trimmed();
  if (!cleaned.startsWith(QLatin1Char('#'))) cleaned.prepend(QLatin1Char('#'));
  const QColor c(cleaned);
  return c.isValid() ? c : fallback;
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

/// True when the session already has an explicit Goal tag for the same team tied to \p source
/// (overlapping clip or goal confirmation shortly after the originating tag).
bool hasExplicitGoalTagForTeam(const QVector<TagSession::GameTag>& tags,
                               const TagSession::GameTag& source) {
  if (source.team.isEmpty()) return false;
  const QPair<qint64, qint64> sourceInterval = exportIntervalFor(source);
  constexpr qint64 kGoalConfirmWindowAfterOriginMs = 60000;
  for (const auto& tag : tags) {
    if (tag.mainEvent != QStringLiteral("Goal")) continue;
    if (tag.team != source.team) continue;
    const QPair<qint64, qint64> goalInterval = exportIntervalFor(tag);
    if (goalInterval.first <= sourceInterval.second &&
        goalInterval.second >= sourceInterval.first) {
      return true;
    }
    if (tag.positionMs >= source.positionMs &&
        tag.positionMs - source.positionMs <= kGoalConfirmWindowAfterOriginMs) {
      return true;
    }
  }
  return false;
}

/// Adds synthetic Goal tags when a scored goal is implied by follow-up but never tagged as Goal.
QVector<TagSession::GameTag> tagsForExport(const QVector<TagSession::GameTag>& sortedTags) {
  QVector<TagSession::GameTag> expanded = sortedTags;
  expanded.reserve(sortedTags.size() + 8);
  for (const auto& tag : sortedTags) {
    if (tag.mainEvent == QStringLiteral("Goal")) continue;
    if (!followUpPathContainsScoredGoal(tag.followUpEvent)) continue;
    if (hasExplicitGoalTagForTeam(sortedTags, tag)) continue;

    TagSession::GameTag goalTag = tag;
    goalTag.mainEvent = QStringLiteral("Goal");
    goalTag.followUpEvent.clear();
    // Use Goal lead/lag from EventDefaults unless the originating clip was manually trimmed.
    if (!tag.intervalManuallyEdited) {
      goalTag.intervalManuallyEdited = false;
    }
    expanded.append(goalTag);
  }

  std::stable_sort(expanded.begin(), expanded.end(),
                   [](const TagSession::GameTag& a, const TagSession::GameTag& b) {
                     if (a.startMs != b.startMs) return a.startMs < b.startMs;
                     if (a.positionMs != b.positionMs) return a.positionMs < b.positionMs;
                     // Emit the originating event before the derived Goal at the same timestamp.
                     const bool aIsGoal = a.mainEvent == QStringLiteral("Goal");
                     const bool bIsGoal = b.mainEvent == QStringLiteral("Goal");
                     if (aIsGoal != bIsGoal) return aIsGoal;
                     return false;
                   });
  return expanded;
}

/// Returns the running goal counts (home, away) for goals tagged at or before \p positionMs.
QPair<int, int> runningScoreAt(const QVector<TagSession::GameTag>& tags, qint64 positionMs) {
  int home = 0;
  int away = 0;
  for (const auto& tag : tags) {
    if (tag.mainEvent != QStringLiteral("Goal")) continue;
    if (tag.positionMs > positionMs) continue;
    if (tag.team == QStringLiteral("Home")) ++home;
    else if (tag.team == QStringLiteral("Away")) ++away;
  }
  return {home, away};
}

/// Turns a single GameTag into its zero, one, or two emitted XML instances.
QVector<EmittedInstance> emittedInstancesFor(const TagSession::GameTag& tag,
                                             const QString& homeAbbrev,
                                             const QString& awayAbbrev) {
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
  const QString shortCode = EventCodeMap::shortCodeForMainEvent(tag.mainEvent);
  const bool hasAbbrevs = !homeAbbrev.isEmpty() && !awayAbbrev.isEmpty();
  const QString taggedAbbrev =
      tag.team == QStringLiteral("Home") ? homeAbbrev :
      tag.team == QStringLiteral("Away") ? awayAbbrev : QString();
  const QString opposingAbbrev =
      tag.team == QStringLiteral("Home") ? awayAbbrev :
      tag.team == QStringLiteral("Away") ? homeAbbrev : QString();

  if (shortCode.isEmpty() || !hasAbbrevs || taggedAbbrev.isEmpty()) {
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
  positive.code = QStringLiteral("%1 %2+").arg(taggedAbbrev, shortCode);
  positive.period = tag.period;
  result.append(positive);

  EmittedInstance negative;
  negative.startMs = exportStartMs;
  negative.endMs = exportEndMs;
  negative.code = QStringLiteral("%1 %2-").arg(opposingAbbrev, shortCode);
  negative.period = tag.period;
  result.append(negative);

  return result;
}

/// RGB triple in 16-bit Olympia/LongoMatch format.
struct Rgb16 {
  int r = 0;
  int g = 0;
  int b = 0;
};

Rgb16 colorForCode(const QString& code,
                   const QString& homeAbbrev,
                   const QString& awayAbbrev,
                   const QColor& homeColor,
                   const QColor& awayColor) {
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

  auto colorFromTeam = [&](const QColor& teamColor) {
    return Rgb16{eightBitToSixteenBit(teamColor.red()),
                 eightBitToSixteenBit(teamColor.green()),
                 eightBitToSixteenBit(teamColor.blue())};
  };

  if (!homeAbbrev.isEmpty() && code.startsWith(homeAbbrev + QLatin1Char(' '))) {
    return colorFromTeam(homeColor);
  }
  if (!awayAbbrev.isEmpty() && code.startsWith(awayAbbrev + QLatin1Char(' '))) {
    return colorFromTeam(awayColor);
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

  // Sort tags chronologically by their interval start so the IDs reflect timeline order.
  QVector<TagSession::GameTag> sortedTags = session->tags();
  std::stable_sort(sortedTags.begin(), sortedTags.end(),
                   [](const TagSession::GameTag& a, const TagSession::GameTag& b) {
                     if (a.startMs != b.startMs) return a.startMs < b.startMs;
                     return a.positionMs < b.positionMs;
                   });

  const QVector<TagSession::GameTag> exportTags = tagsForExport(sortedTags);

  const QString homeAbbrev = session->homeAbbrev();
  const QString awayAbbrev = session->awayAbbrev();
  const QString homeName = session->homeTeamName();
  const QString awayName = session->awayTeamName();
  const QString competitionName = session->competitionName();
  const int gameYear = session->gameYear();
  const QColor homeColor = parseHexColor(session->homeTeamColor(), QColor(60, 90, 200));
  const QColor awayColor = parseHexColor(session->awayTeamColor(), QColor(200, 60, 60));

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

  // Track every unique <code> we emit so the <ROWS> palette can list them all.
  QSet<QString> emittedCodes;
  // Preserve emission order so the palette rows appear in the same order as the codes
  // first appeared in the timeline (matches the reference XML's ordering).
  QStringList emittedCodesOrder;

  int nextInstanceId = 1;
  for (const auto& tag : exportTags) {
    const QVector<EmittedInstance> instances = emittedInstancesFor(tag, homeAbbrev, awayAbbrev);
    if (instances.isEmpty()) continue;

    const QPair<int, int> score = runningScoreAt(exportTags, tag.positionMs);
    const QString resultadoLabel =
        (homeAbbrev.isEmpty() && awayAbbrev.isEmpty())
            ? QString()
            : QStringLiteral("%1 %2 - %3 %4").arg(homeAbbrev.isEmpty() ? homeName : homeAbbrev)
                  .arg(score.first).arg(score.second)
                  .arg(awayAbbrev.isEmpty() ? awayName : awayAbbrev);

    for (const auto& instance : instances) {
      writer.writeStartElement(QStringLiteral("instance"));
      writer.writeTextElement(QStringLiteral("ID"), QString::number(nextInstanceId++));
      writer.writeTextElement(QStringLiteral("start"), secondsString(instance.startMs));
      writer.writeTextElement(QStringLiteral("end"), secondsString(instance.endMs));
      writer.writeTextElement(QStringLiteral("code"), instance.code);

      if (!emittedCodes.contains(instance.code)) {
        emittedCodes.insert(instance.code);
        emittedCodesOrder.append(instance.code);
      }

      if (instance.includeMatchLabels) {
        if (!competitionName.isEmpty()) {
          writer.writeStartElement(QStringLiteral("label"));
          writer.writeTextElement(QStringLiteral("group"), QStringLiteral("COMPETICION"));
          writer.writeTextElement(QStringLiteral("text"), competitionName);
          writer.writeEndElement();
        }
        if (!resultadoLabel.isEmpty()) {
          writer.writeStartElement(QStringLiteral("label"));
          writer.writeTextElement(QStringLiteral("group"), QStringLiteral("RESULTADO"));
          writer.writeTextElement(QStringLiteral("text"), resultadoLabel);
          writer.writeEndElement();
        }
        if (!instance.period.isEmpty()) {
          writer.writeStartElement(QStringLiteral("label"));
          writer.writeTextElement(QStringLiteral("group"), QStringLiteral("QUARTOS"));
          writer.writeTextElement(QStringLiteral("text"), instance.period);
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
    const Rgb16 rgb = colorForCode(code, homeAbbrev, awayAbbrev, homeColor, awayColor);
    writer.writeStartElement(QStringLiteral("row"));
    writer.writeTextElement(QStringLiteral("code"), code);
    writer.writeTextElement(QStringLiteral("R"), QString::number(rgb.r));
    writer.writeTextElement(QStringLiteral("G"), QString::number(rgb.g));
    writer.writeTextElement(QStringLiteral("B"), QString::number(rgb.b));
    writer.writeEndElement(); // row
  }
  writer.writeEndElement(); // ROWS

  writer.writeEndElement(); // file
  writer.writeEndDocument();

  if (writer.hasError()) {
    if (errorMessage) {
      *errorMessage = QStringLiteral("XML writer reported an error.");
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
