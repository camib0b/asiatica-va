#include "XmlImporter.h"

#include "EventDefaults.h"
#include "TimeConvert.h"

#include <QFile>
#include <QStringList>
#include <QXmlStreamReader>

#include <algorithm>
#include <climits>
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
  if (seconds > static_cast<double>(LLONG_MAX) / 1000.0 || !std::isfinite(seconds)) {
    if (ok) *ok = false;
    return 0;
  }
  if (ok) *ok = true;
  return TimeConvert::millisecondsFromSeconds(seconds);
}

void sortInstances(QVector<ParsedInstance>& instances) {
  std::stable_sort(instances.begin(), instances.end(),
                   [](const ParsedInstance& a, const ParsedInstance& b) {
                     if (a.startMs != b.startMs) return a.startMs < b.startMs;
                     return a.xmlId < b.xmlId;
                   });
}

struct InstanceDraft {
  ParsedInstance instance;
  bool sawId = false;
  bool sawStart = false;
  bool sawEnd = false;
  bool sawCode = false;
  QString labelGroup;
  QString labelText;
};

class LongoMatchInstanceParser {
public:
  LongoMatchInstanceParser(QXmlStreamReader& reader, QString* errorMessage)
      : reader_(reader), errorMessage_(errorMessage) {}

  bool parse(QVector<ParsedInstance>* instances) {
    while (!reader_.atEnd()) {
      reader_.readNext();
      if (reader_.isStartElement()) {
        if (!onStartElement()) return false;
      } else if (reader_.isEndElement()) {
        if (!onEndElement()) return false;
      }
    }

    if (reader_.hasError()) {
      return fail(QStringLiteral("XML parse error: ") + reader_.errorString());
    }
    if (scope_ == Scope::Instance || scope_ == Scope::Label) {
      return fail(QStringLiteral("XML ended while an <instance> was still open."));
    }
    if (parsed_.isEmpty()) {
      return fail(QStringLiteral("No instances found in XML file."));
    }

    sortInstances(parsed_);
    *instances = std::move(parsed_);
    return true;
  }

private:
  enum class Scope { Document, AllInstances, Instance, Label };

  bool fail(const QString& message) {
    if (errorMessage_) *errorMessage_ = message;
    return false;
  }

  QString describeCurrentInstance() const {
    if (draft_.sawId) {
      return QStringLiteral("instance ID %1").arg(draft_.instance.xmlId);
    }
    return QStringLiteral("instance at line %1").arg(reader_.lineNumber());
  }

  QString readLeafText() {
    return reader_.readElementText(QXmlStreamReader::ErrorOnUnexpectedElement).trimmed();
  }

  bool onStartElement() {
    const QString name = reader_.name().toString();

    if (name == QStringLiteral("ALL_INSTANCES")) {
      if (scope_ != Scope::Document) {
        return fail(QStringLiteral("Unexpected nested <ALL_INSTANCES> at line %1.")
                        .arg(reader_.lineNumber()));
      }
      scope_ = Scope::AllInstances;
      return true;
    }

    if (name == QStringLiteral("instance")) {
      if (scope_ != Scope::AllInstances) {
        return fail(QStringLiteral("Unexpected <instance> outside <ALL_INSTANCES> at line %1.")
                        .arg(reader_.lineNumber()));
      }
      scope_ = Scope::Instance;
      draft_ = InstanceDraft{};
      return true;
    }

    if (scope_ == Scope::Document) {
      reader_.skipCurrentElement();
      return true;
    }

    if (name == QStringLiteral("label")) {
      if (scope_ != Scope::Instance) {
        reader_.skipCurrentElement();
        return true;
      }
      scope_ = Scope::Label;
      draft_.labelGroup.clear();
      draft_.labelText.clear();
      return true;
    }

    if (name == QStringLiteral("ID")) return readId();
    if (name == QStringLiteral("start")) return readStartEnd(true);
    if (name == QStringLiteral("end")) return readStartEnd(false);
    if (name == QStringLiteral("code")) return readCode();
    if (name == QStringLiteral("group")) return readLabelGroup();
    if (name == QStringLiteral("text")) return readLabelText();

    reader_.skipCurrentElement();
    return true;
  }

  bool onEndElement() {
    const QString name = reader_.name().toString();

    if (name == QStringLiteral("label")) {
      if (scope_ != Scope::Label) return true;
      if (draft_.labelGroup == QStringLiteral("QUARTOS") &&
          draft_.instance.periodLabel.isEmpty() && !draft_.labelText.isEmpty()) {
        draft_.instance.periodLabel = draft_.labelText;
      }
      scope_ = Scope::Instance;
      return true;
    }

    if (name == QStringLiteral("instance")) {
      if (scope_ == Scope::Label) {
        return fail(QStringLiteral("Unclosed <label> in %1.").arg(describeCurrentInstance()));
      }
      if (scope_ != Scope::Instance) return true;
      if (!finishInstance()) return false;
      scope_ = Scope::AllInstances;
      return true;
    }

    if (name == QStringLiteral("ALL_INSTANCES")) {
      if (scope_ == Scope::Instance || scope_ == Scope::Label) {
        return fail(QStringLiteral("Unclosed <instance> inside <ALL_INSTANCES>."));
      }
      scope_ = Scope::Document;
    }
    return true;
  }

  bool readId() {
    if (scope_ != Scope::Instance) {
      reader_.skipCurrentElement();
      return true;
    }
    if (draft_.sawId) {
      return fail(QStringLiteral("Duplicate <ID> in %1.").arg(describeCurrentInstance()));
    }
    const QString text = readLeafText();
    if (reader_.hasError()) {
      return fail(QStringLiteral("XML parse error: ") + reader_.errorString());
    }
    if (text.isEmpty()) {
      return fail(QStringLiteral("Missing or empty <ID> value in %1.").arg(describeCurrentInstance()));
    }
    bool ok = false;
    draft_.instance.xmlId = text.toInt(&ok);
    if (!ok) {
      return fail(QStringLiteral("Invalid <ID> value in %1.").arg(describeCurrentInstance()));
    }
    draft_.sawId = true;
    return true;
  }

  bool readStartEnd(bool isStart) {
    const QString fieldName = isStart ? QStringLiteral("start") : QStringLiteral("end");
    if (scope_ != Scope::Instance) {
      reader_.skipCurrentElement();
      return true;
    }
    const bool alreadySaw = isStart ? draft_.sawStart : draft_.sawEnd;
    if (alreadySaw) {
      return fail(QStringLiteral("Duplicate <%1> in %2.").arg(fieldName, describeCurrentInstance()));
    }
    const QString text = readLeafText();
    if (reader_.hasError()) {
      return fail(QStringLiteral("XML parse error: ") + reader_.errorString());
    }
    if (text.isEmpty()) {
      return fail(QStringLiteral("Missing or empty <%1> value in %2.")
                      .arg(fieldName, describeCurrentInstance()));
    }
    bool ok = false;
    const qint64 milliseconds = secondsTextToMs(text, &ok);
    if (!ok) {
      return fail(QStringLiteral("Invalid <%1> value in %2.")
                      .arg(fieldName, describeCurrentInstance()));
    }
    if (isStart) {
      draft_.instance.startMs = milliseconds;
      draft_.sawStart = true;
    } else {
      draft_.instance.endMs = milliseconds;
      draft_.sawEnd = true;
    }
    return true;
  }

  bool readCode() {
    if (scope_ != Scope::Instance) {
      reader_.skipCurrentElement();
      return true;
    }
    if (draft_.sawCode) {
      return fail(QStringLiteral("Duplicate <code> in %1.").arg(describeCurrentInstance()));
    }
    const QString text = readLeafText();
    if (reader_.hasError()) {
      return fail(QStringLiteral("XML parse error: ") + reader_.errorString());
    }
    if (text.isEmpty()) {
      return fail(QStringLiteral("%1 has an empty <code>.").arg(describeCurrentInstance()));
    }
    draft_.instance.code = text;
    draft_.sawCode = true;
    return true;
  }

  bool readLabelGroup() {
    if (scope_ != Scope::Label) {
      reader_.skipCurrentElement();
      return true;
    }
    draft_.labelGroup = readLeafText();
    if (reader_.hasError()) {
      return fail(QStringLiteral("XML parse error: ") + reader_.errorString());
    }
    return true;
  }

  bool readLabelText() {
    if (scope_ != Scope::Label) {
      reader_.skipCurrentElement();
      return true;
    }
    draft_.labelText = readLeafText();
    if (reader_.hasError()) {
      return fail(QStringLiteral("XML parse error: ") + reader_.errorString());
    }
    return true;
  }

  bool finishInstance() {
    QStringList missingFields;
    if (!draft_.sawId) missingFields.append(QStringLiteral("ID"));
    if (!draft_.sawStart) missingFields.append(QStringLiteral("start"));
    if (!draft_.sawEnd) missingFields.append(QStringLiteral("end"));
    if (!draft_.sawCode) missingFields.append(QStringLiteral("code"));
    if (!missingFields.isEmpty()) {
      return fail(QStringLiteral("%1 is missing required <%2>.")
                      .arg(describeCurrentInstance(), missingFields.join(QStringLiteral(">, <"))));
    }
    if (draft_.instance.endMs < draft_.instance.startMs) {
      return fail(QStringLiteral("%1 has end before start.").arg(describeCurrentInstance()));
    }
    parsed_.append(draft_.instance);
    return true;
  }

  QXmlStreamReader& reader_;
  QString* errorMessage_ = nullptr;
  Scope scope_ = Scope::Document;
  InstanceDraft draft_;
  QVector<ParsedInstance> parsed_;
};

} // namespace

bool parse(const QString& filePath, QVector<ParsedInstance>* instances, QString* errorMessage) {
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
  LongoMatchInstanceParser parser(reader, errorMessage);
  return parser.parse(instances);
}

SyncAnchorResult syncAnchorInstance(const QVector<ParsedInstance>& instances) {
  SyncAnchorResult result;
  const ParsedInstance* startAnchor = nullptr;
  const ParsedInstance* quarter1 = nullptr;
  const ParsedInstance* firstValid = nullptr;

  for (const ParsedInstance& instance : instances) {
    if (instance.code.isEmpty()) continue;
    if (!firstValid) firstValid = &instance;
    if (!startAnchor && instance.code == QLatin1String(EventDefaults::TimeCodes::kStartAnchor)) {
      startAnchor = &instance;
    } else if (!quarter1 && instance.code == QLatin1String(EventDefaults::TimeCodes::kQuarter1)) {
      quarter1 = &instance;
    }
  }

  const ParsedInstance* selected = startAnchor ? startAnchor : (quarter1 ? quarter1 : firstValid);
  if (!selected) return result;

  result.found = true;
  result.usedFallback = (selected != startAnchor);
  result.instance = *selected;
  return result;
}

} // namespace XmlImporter
