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

ParseResult fromJson(const QJsonValue& value) {
  ParseResult result;
  if (value.isUndefined() || value.isNull()) {
    result.ok = true;
    return result;
  }
  if (!value.isArray()) {
    result.errorMessage = labelOverrideError(QStringLiteral("expected an array."));
    return result;
  }

  QVector<EventLabelOverrides::Record> records;
  const QJsonArray array = value.toArray();
  records.reserve(array.size());
  for (const QJsonValue& item : array) {
    EventLabelOverrides::Record record;
    if (!parseRecord(item, &record, &result.errorMessage)) return result;
    records.append(record);
  }
  if (!result.setup.labelOverrides.replaceRecords(records)) {
    result.errorMessage =
        labelOverrideError(QStringLiteral("the table has an unknown id or a duplicate visible label."));
    return result;
  }
  result.ok = true;
  return result;
}

}  // namespace EventControlsSetup
