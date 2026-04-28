#include "XmlExporter.h"

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

/// Returns the short XML code for a canonical main-event name, or empty when the event
/// has no team-affiliated short code (i.e. it is a neutral / pass-through code).
QString shortCodeForMainEvent(const QString& canonicalMainEvent) {
  static const QHash<QString, QString> kMap = {
      {QStringLiteral("Goal"),         QStringLiteral("GOAL")},
      {QStringLiteral("Shot"),         QStringLiteral("SHOT")},
      {QStringLiteral("PC"),           QStringLiteral("PC")},
      {QStringLiteral("PC Foul"),      QStringLiteral("PCF")},
      {QStringLiteral("Card"),         QStringLiteral("CARD")},
      {QStringLiteral("Pass"),         QStringLiteral("PASS")},
      {QStringLiteral("Circle Entry"), QStringLiteral("ENTRY")},
      {QStringLiteral("16-yd"),        QStringLiteral("16YD")},
      {QStringLiteral("50-yd"),        QStringLiteral("50YD")},
      {QStringLiteral("75-yd"),        QStringLiteral("75YD")},
      {QStringLiteral("Turnover"),     QStringLiteral("TO")},
      {QStringLiteral("Special"),      QStringLiteral("SPC")},
      {QStringLiteral("PS"),           QStringLiteral("PS")},
      {QStringLiteral("S.O."),         QStringLiteral("SO")},
  };
  return kMap.value(canonicalMainEvent);
}

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
  QString period;        // empty = no QUARTOS label
  QString followUpEvent; // empty = no FOLLOW_UP label
  bool includeMatchLabels = true;
};

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

  // Neutral / pass-through code (Q1..Q4, Inicio, TM): one <instance>, no team affiliation.
  const QString neutralCode = neutralPassThroughCode(tag.mainEvent);
  if (!neutralCode.isEmpty()) {
    EmittedInstance instance;
    instance.startMs = tag.startMs;
    instance.endMs = tag.endMs;
    instance.code = neutralCode;
    instance.period = tag.period;
    instance.followUpEvent = tag.followUpEvent;
    // Quarter / start-anchor instances do not carry the per-event metadata labels in the
    // reference (see <ID>1 Q1, <ID>3 Inicio, <ID>13 TM examples), so suppress them.
    instance.includeMatchLabels = false;
    result.append(instance);
    return result;
  }

  // Team-affiliated code: requires both team abbreviation and a short code mapping. When
  // either is missing we still emit a single neutral <code> using the canonical event name
  // so the user does not silently lose information.
  const QString shortCode = shortCodeForMainEvent(tag.mainEvent);
  const bool hasAbbrevs = !homeAbbrev.isEmpty() && !awayAbbrev.isEmpty();
  const QString taggedAbbrev =
      tag.team == QStringLiteral("Home") ? homeAbbrev :
      tag.team == QStringLiteral("Away") ? awayAbbrev : QString();
  const QString opposingAbbrev =
      tag.team == QStringLiteral("Home") ? awayAbbrev :
      tag.team == QStringLiteral("Away") ? homeAbbrev : QString();

  if (shortCode.isEmpty() || !hasAbbrevs || taggedAbbrev.isEmpty()) {
    EmittedInstance instance;
    instance.startMs = tag.startMs;
    instance.endMs = tag.endMs;
    instance.code = tag.mainEvent;
    instance.period = tag.period;
    instance.followUpEvent = tag.followUpEvent;
    result.append(instance);
    return result;
  }

  EmittedInstance positive;
  positive.startMs = tag.startMs;
  positive.endMs = tag.endMs;
  positive.code = QStringLiteral("%1 %2+").arg(taggedAbbrev, shortCode);
  positive.period = tag.period;
  positive.followUpEvent = tag.followUpEvent;
  result.append(positive);

  EmittedInstance negative;
  negative.startMs = tag.startMs;
  negative.endMs = tag.endMs;
  negative.code = QStringLiteral("%1 %2-").arg(opposingAbbrev, shortCode);
  negative.period = tag.period;
  negative.followUpEvent = tag.followUpEvent;
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
  for (const auto& tag : sortedTags) {
    const QVector<EmittedInstance> instances = emittedInstancesFor(tag, homeAbbrev, awayAbbrev);
    if (instances.isEmpty()) continue;

    const QPair<int, int> score = runningScoreAt(sortedTags, tag.positionMs);
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
        if (!instance.followUpEvent.isEmpty()) {
          writer.writeStartElement(QStringLiteral("label"));
          writer.writeTextElement(QStringLiteral("group"), QStringLiteral("FOLLOW_UP"));
          writer.writeTextElement(QStringLiteral("text"), instance.followUpEvent);
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
