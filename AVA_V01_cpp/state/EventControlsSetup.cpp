#include "EventControlsSetup.h"

#include "TagSession.h"
#include "Utf8JsonFile.h"

#include <QJsonArray>
#include <QJsonObject>

#include <limits>

namespace EventControlsSetup {
namespace {

QString labelOverrideError(const QString& detail) {
  return QStringLiteral("Label overrides: ") + detail;
}

QString pathOverrideError(const QString& detail) {
  return QStringLiteral("Path overrides: ") + detail;
}

bool parseRecord(const QJsonValue& value, EventLabelOverrides::Record* record, QString* errorMessage) {
  if (!value.isObject()) {
    *errorMessage = labelOverrideError(QStringLiteral("each entry must be an object."));
    return false;
  }
  const QJsonObject object = value.toObject();
  qint64 eventId = 0;
  if (!Utf8JsonFile::wholeNumber(object.value(QStringLiteral("eventId")), &eventId) ||
      eventId < std::numeric_limits<int>::min() || eventId > std::numeric_limits<int>::max()) {
    *errorMessage = labelOverrideError(QStringLiteral("eventId must be an integer."));
    return false;
  }
  record->id = static_cast<EventCatalog::EventId>(static_cast<int>(eventId));

  if (object.contains(QStringLiteral("label"))) {
    const QJsonValue label = object.value(QStringLiteral("label"));
    if (!label.isString()) {
      *errorMessage = labelOverrideError(QStringLiteral("label must be a string."));
      return false;
    }
    record->label = label.toString();
  }

  if (object.contains(QStringLiteral("shortcutKey"))) {
    qint64 shortcutKey = 0;
    if (!Utf8JsonFile::wholeNumber(object.value(QStringLiteral("shortcutKey")), &shortcutKey) ||
        shortcutKey < std::numeric_limits<int>::min() ||
        shortcutKey > std::numeric_limits<int>::max()) {
      *errorMessage = labelOverrideError(QStringLiteral("shortcutKey must be an integer."));
      return false;
    }
    const auto key = static_cast<Qt::Key>(static_cast<int>(shortcutKey));
    if (key != Qt::Key_unknown) {
      record->hasShortcutKey = true;
      record->shortcutKey = key;
    }
  }

  if (object.contains(QStringLiteral("hidden"))) {
    const QJsonValue hidden = object.value(QStringLiteral("hidden"));
    if (!hidden.isBool()) {
      *errorMessage = labelOverrideError(QStringLiteral("hidden must be a boolean."));
      return false;
    }
    record->hidden = hidden.toBool();
  }
  return true;
}

bool parsePathRecord(const QJsonValue& value, EventLabelOverrides::PathRecord* record,
                     QString* errorMessage) {
  if (!value.isObject()) {
    *errorMessage = pathOverrideError(QStringLiteral("each entry must be an object."));
    return false;
  }
  const QJsonObject object = value.toObject();
  qint64 mainEventId = 0;
  if (!Utf8JsonFile::wholeNumber(object.value(QStringLiteral("mainEventId")), &mainEventId) ||
      mainEventId < std::numeric_limits<int>::min() ||
      mainEventId > std::numeric_limits<int>::max()) {
    *errorMessage = pathOverrideError(QStringLiteral("mainEventId must be an integer."));
    return false;
  }
  record->path.mainEventId = static_cast<EventCatalog::EventId>(static_cast<int>(mainEventId));

  const QJsonValue chainValue = object.value(QStringLiteral("chain"));
  if (!chainValue.isArray()) {
    *errorMessage = pathOverrideError(QStringLiteral("chain must be an array."));
    return false;
  }
  const QJsonArray chain = chainValue.toArray();
  if (chain.isEmpty() || chain.size() > EventCatalog::kMaxFollowUpDepth) {
    *errorMessage = pathOverrideError(QStringLiteral("chain must list one to three follow-ups."));
    return false;
  }
  record->path.stepCount = chain.size();
  for (int index = 0; index < chain.size(); ++index) {
    qint64 stepId = 0;
    if (!Utf8JsonFile::wholeNumber(chain.at(index), &stepId) ||
        stepId < std::numeric_limits<int>::min() || stepId > std::numeric_limits<int>::max()) {
      *errorMessage = pathOverrideError(QStringLiteral("chain ids must be integers."));
      return false;
    }
    record->path.steps[static_cast<std::size_t>(index)] =
        static_cast<EventCatalog::EventId>(static_cast<int>(stepId));
  }

  if (object.contains(QStringLiteral("label"))) {
    const QJsonValue label = object.value(QStringLiteral("label"));
    if (!label.isString()) {
      *errorMessage = pathOverrideError(QStringLiteral("label must be a string."));
      return false;
    }
    record->label = label.toString();
  }
  if (object.contains(QStringLiteral("hidden"))) {
    const QJsonValue hidden = object.value(QStringLiteral("hidden"));
    if (!hidden.isBool()) {
      *errorMessage = pathOverrideError(QStringLiteral("hidden must be a boolean."));
      return false;
    }
    record->hasHidden = true;
    record->hidden = hidden.toBool();
  }
  return true;
}

}  // namespace

Setup builtinDefault() { return {}; }

QString builtinDefaultName() { return QStringLiteral("Camila's layout"); }

bool isBuiltinDefaultName(const QString& name) {
  return name.trimmed().compare(builtinDefaultName(), Qt::CaseInsensitive) == 0;
}

Setup fromSession(const TagSession& session) {
  Setup setup;
  setup.labelOverrides = session.eventLabelOverrides();
  return setup;
}

ApplyStatus applyToSession(TagSession& session, const Setup& setup) {
  if (!session.replaceEventLabelOverrides(setup.labelOverrides)) return ApplyStatus::Rejected;
  return ApplyStatus::Applied;
}

QJsonArray toJson(const Setup& setup) {
  QJsonArray array;
  const QVector<EventLabelOverrides::Record> records = setup.labelOverrides.records();
  for (const EventLabelOverrides::Record& record : records) {
    QJsonObject object;
    object.insert(QStringLiteral("eventId"), static_cast<int>(record.id));
    if (!record.label.isEmpty()) object.insert(QStringLiteral("label"), record.label);
    if (record.hasShortcutKey && record.shortcutKey != Qt::Key_unknown) {
      object.insert(QStringLiteral("shortcutKey"), static_cast<int>(record.shortcutKey));
    }
    if (record.hidden) object.insert(QStringLiteral("hidden"), true);
    array.append(object);
  }
  return array;
}

QJsonArray pathOverridesToJson(const Setup& setup) {
  QJsonArray array;
  const QVector<EventLabelOverrides::PathRecord> records = setup.labelOverrides.pathRecords();
  for (const EventLabelOverrides::PathRecord& record : records) {
    QJsonObject object;
    object.insert(QStringLiteral("mainEventId"), static_cast<int>(record.path.mainEventId));
    QJsonArray chain;
    const int count = record.path.stepCount;
    for (int index = 0; index < count && index < EventCatalog::kMaxFollowUpDepth; ++index) {
      chain.append(static_cast<int>(record.path.steps[static_cast<std::size_t>(index)]));
    }
    object.insert(QStringLiteral("chain"), chain);
    if (!record.label.isEmpty()) object.insert(QStringLiteral("label"), record.label);
    if (record.hasHidden) object.insert(QStringLiteral("hidden"), record.hidden);
    array.append(object);
  }
  return array;
}

ParseResult fromJson(const QJsonValue& labelOverrides, const QJsonValue& pathOverrides) {
  ParseResult result;
  QVector<EventLabelOverrides::Record> records;
  if (!labelOverrides.isUndefined() && !labelOverrides.isNull()) {
    if (!labelOverrides.isArray()) {
      result.errorMessage = labelOverrideError(QStringLiteral("expected an array."));
      return result;
    }
    const QJsonArray array = labelOverrides.toArray();
    records.reserve(array.size());
    for (const QJsonValue& item : array) {
      EventLabelOverrides::Record record;
      if (!parseRecord(item, &record, &result.errorMessage)) return result;
      records.append(record);
    }
  }

  QVector<EventLabelOverrides::PathRecord> paths;
  if (!pathOverrides.isUndefined() && !pathOverrides.isNull()) {
    if (!pathOverrides.isArray()) {
      result.errorMessage = pathOverrideError(QStringLiteral("expected an array."));
      return result;
    }
    const QJsonArray array = pathOverrides.toArray();
    paths.reserve(array.size());
    for (const QJsonValue& item : array) {
      EventLabelOverrides::PathRecord record;
      if (!parsePathRecord(item, &record, &result.errorMessage)) return result;
      paths.append(record);
    }
  }

  if (!result.setup.labelOverrides.replaceAll(records, paths)) {
    result.errorMessage = labelOverrideError(
        QStringLiteral("the table has an unknown id, an unknown path, or a duplicate visible label."));
    return result;
  }
  result.ok = true;
  return result;
}

}  // namespace EventControlsSetup
