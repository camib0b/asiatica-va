#include "XmlImporter.h"

#include "EventDefaults.h"

#include <QFile>
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>

namespace XmlImporter {

namespace {

qint64 secondsTextToMs(const QString& text, bool* ok) {
  bool parseOk = false;
  const double seconds = text.trimmed().toDouble(&parseOk);
  if (!parseOk || seconds < 0.0) {
    if (ok) *ok = false;
    return 0;
  }
  if (ok) *ok = true;
  return static_cast<qint64>(std::llround(seconds * 1000.0));
}

void sortInstances(QVector<ParsedInstance>& instances) {
  std::stable_sort(instances.begin(), instances.end(),
                   [](const ParsedInstance& a, const ParsedInstance& b) {
                     if (a.startMs != b.startMs) return a.startMs < b.startMs;
                     return a.xmlId < b.xmlId;
                   });
}

} // namespace

bool parse(const QString& filePath,
           QVector<ParsedInstance>* instances,
           QString* errorMessage) {
  if (!instances) {
    if (errorMessage) *errorMessage = QStringLiteral("No output container provided.");
    return false;
  }
  instances->clear();

  if (filePath.trimmed().isEmpty()) {
    if (errorMessage) *errorMessage = QStringLiteral("No file path provided.");
    return false;
  }

  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    if (errorMessage) {
      *errorMessage = QStringLiteral("Failed to open file: ") + file.errorString();
    }
    return false;
  }

  QXmlStreamReader reader(&file);
  bool inAllInstances = false;
  bool inInstance = false;
  ParsedInstance current;
  QString currentLabelGroup;
  bool collectingLabelText = false;

  while (!reader.atEnd()) {
    reader.readNext();

    if (reader.isStartElement()) {
      const QString name = reader.name().toString();
      if (name == QStringLiteral("ALL_INSTANCES")) {
        inAllInstances = true;
        continue;
      }
      if (!inAllInstances) continue;

      if (name == QStringLiteral("instance")) {
        inInstance = true;
        current = ParsedInstance{};
        currentLabelGroup.clear();
        collectingLabelText = false;
        continue;
      }
      if (!inInstance) continue;

      if (name == QStringLiteral("ID")) {
        reader.readNext();
        if (reader.isCharacters()) {
          current.xmlId = reader.text().toString().trimmed().toInt();
        }
        continue;
      }
      if (name == QStringLiteral("start")) {
        reader.readNext();
        if (reader.isCharacters()) {
          bool ok = false;
          const qint64 ms = secondsTextToMs(reader.text().toString(), &ok);
          if (!ok) {
            if (errorMessage) {
              *errorMessage = QStringLiteral("Invalid <start> value in instance ID %1.")
                                  .arg(current.xmlId);
            }
            return false;
          }
          current.startMs = ms;
        }
        continue;
      }
      if (name == QStringLiteral("end")) {
        reader.readNext();
        if (reader.isCharacters()) {
          bool ok = false;
          const qint64 ms = secondsTextToMs(reader.text().toString(), &ok);
          if (!ok) {
            if (errorMessage) {
              *errorMessage = QStringLiteral("Invalid <end> value in instance ID %1.")
                                  .arg(current.xmlId);
            }
            return false;
          }
          current.endMs = ms;
        }
        continue;
      }
      if (name == QStringLiteral("code")) {
        reader.readNext();
        if (reader.isCharacters()) {
          current.code = reader.text().toString().trimmed();
        }
        continue;
      }
      if (name == QStringLiteral("group")) {
        reader.readNext();
        if (reader.isCharacters()) {
          currentLabelGroup = reader.text().toString().trimmed();
        }
        continue;
      }
      if (name == QStringLiteral("text") && collectingLabelText) {
        reader.readNext();
        if (reader.isCharacters() &&
            currentLabelGroup == QStringLiteral("QUARTOS") &&
            current.periodLabel.isEmpty()) {
          current.periodLabel = reader.text().toString().trimmed();
        }
        continue;
      }
      if (name == QStringLiteral("label")) {
        currentLabelGroup.clear();
        collectingLabelText = true;
        continue;
      }
    }

    if (reader.isEndElement()) {
      const QString name = reader.name().toString();
      if (name == QStringLiteral("label")) {
        collectingLabelText = false;
        currentLabelGroup.clear();
        continue;
      }
      if (name == QStringLiteral("instance") && inInstance) {
        inInstance = false;
        if (current.code.isEmpty()) {
          if (errorMessage) {
            *errorMessage = QStringLiteral("Instance ID %1 has an empty <code>.")
                                .arg(current.xmlId);
          }
          return false;
        }
        if (current.endMs < current.startMs) {
          if (errorMessage) {
            *errorMessage = QStringLiteral("Instance ID %1 has end before start.")
                                .arg(current.xmlId);
          }
          return false;
        }
        instances->append(current);
        continue;
      }
      if (name == QStringLiteral("ALL_INSTANCES")) {
        inAllInstances = false;
      }
    }
  }

  if (reader.hasError()) {
    if (errorMessage) {
      *errorMessage = QStringLiteral("XML parse error: ") + reader.errorString();
    }
    return false;
  }

  if (instances->isEmpty()) {
    if (errorMessage) *errorMessage = QStringLiteral("No instances found in XML file.");
    return false;
  }

  sortInstances(*instances);
  return true;
}

ParsedInstance syncAnchorInstance(const QVector<ParsedInstance>& instances,
                                  bool* usedFallback) {
  if (usedFallback) *usedFallback = false;
  if (instances.isEmpty()) return ParsedInstance{};

  for (const ParsedInstance& instance : instances) {
    if (instance.code == QLatin1String(EventDefaults::TimeCodes::kStartAnchor)) {
      return instance;
    }
  }

  for (const ParsedInstance& instance : instances) {
    if (EventDefaults::isQuarterEvent(instance.code)) {
      if (usedFallback) *usedFallback = true;
      return instance;
    }
  }

  if (usedFallback) *usedFallback = true;
  return instances.first();
}

} // namespace XmlImporter
