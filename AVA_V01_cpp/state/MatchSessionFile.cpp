#include "MatchSessionFile.h"

#include "PresentationQueue.h"
#include "Utf8JsonFile.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>

#include <cmath>
#include <limits>

namespace MatchSessionFile {
namespace {

constexpr char kFormatName[] = "ava-match-session";

QString sessionError(const QString& detail) {
  return QStringLiteral("Match session: ") + detail;
}

LoadResult failedLoad(LoadStatus status, const QString& detail) {
  LoadResult result;
  result.status = status;
  result.errorMessage = sessionError(detail);
  return result;
}

SaveResult failedSave(SaveStatus status, const QString& detail) {
  SaveResult result;
  result.status = status;
  result.errorMessage = sessionError(detail);
  return result;
}

bool requireObject(const QJsonValue& value, QJsonObject* object, QString* errorMessage, const QString& label) {
  if (!value.isObject()) {
    *errorMessage = label + QStringLiteral(" must be an object.");
    return false;
  }
  *object = value.toObject();
  return true;
}

bool optionalString(const QJsonObject& object, const QString& key, QString* text, QString* errorMessage) {
  if (!object.contains(key)) {
    text->clear();
    return true;
  }
  const QJsonValue value = object.value(key);
  if (!value.isString()) {
    *errorMessage = key + QStringLiteral(" must be a string.");
    return false;
  }
  *text = value.toString();
  return true;
}

bool fitsInInt(qint64 number) {
  return number >= std::numeric_limits<int>::min() && number <= std::numeric_limits<int>::max();
}

bool requireWholeNumber(const QJsonObject& object, const QString& key, qint64* number, QString* errorMessage) {
  if (!Utf8JsonFile::wholeNumber(object.value(key), number)) {
    *errorMessage = key + QStringLiteral(" must be an integer.");
    return false;
  }
  return true;
}

QString shapeKindToken(Telestration::ShapeKind kind) {
  switch (kind) {
    case Telestration::ShapeKind::Arrow:
      return QStringLiteral("arrow");
    case Telestration::ShapeKind::DottedArrow:
      return QStringLiteral("dottedArrow");
    case Telestration::ShapeKind::Circle:
      return QStringLiteral("circle");
    case Telestration::ShapeKind::Freehand:
      return QStringLiteral("freehand");
  }
  return {};
}

QString inkColorToken(Telestration::InkColor color) {
  switch (color) {
    case Telestration::InkColor::White:
      return QStringLiteral("white");
    case Telestration::InkColor::Black:
      return QStringLiteral("black");
    case Telestration::InkColor::Blue:
      return QStringLiteral("blue");
    case Telestration::InkColor::Red:
      return QStringLiteral("red");
  }
  return {};
}

bool shapeKindFromToken(const QString& token, Telestration::ShapeKind* kind) {
  if (token == QLatin1String("arrow")) {
    *kind = Telestration::ShapeKind::Arrow;
    return true;
  }
  if (token == QLatin1String("dottedArrow")) {
    *kind = Telestration::ShapeKind::DottedArrow;
    return true;
  }
  if (token == QLatin1String("circle")) {
    *kind = Telestration::ShapeKind::Circle;
    return true;
  }
  if (token == QLatin1String("freehand")) {
    *kind = Telestration::ShapeKind::Freehand;
    return true;
  }
  return false;
}

bool inkColorFromToken(const QString& token, Telestration::InkColor* color) {
  if (token == QLatin1String("white")) {
    *color = Telestration::InkColor::White;
    return true;
  }
  if (token == QLatin1String("black")) {
    *color = Telestration::InkColor::Black;
    return true;
  }
  if (token == QLatin1String("blue")) {
    *color = Telestration::InkColor::Blue;
    return true;
  }
  if (token == QLatin1String("red")) {
    *color = Telestration::InkColor::Red;
    return true;
  }
  return false;
}

QJsonObject pointToJson(const Telestration::Point& point) {
  QJsonObject object;
  object.insert(QStringLiteral("x"), point.x);
  object.insert(QStringLiteral("y"), point.y);
  return object;
}

QJsonObject shapeToJson(const Telestration::Shape& shape) {
  QJsonObject object;
  object.insert(QStringLiteral("kind"), shapeKindToken(shape.kind));
  object.insert(QStringLiteral("color"), inkColorToken(shape.color));
  object.insert(QStringLiteral("strokeWidth"), shape.strokeWidth);
  QJsonArray points;
  for (const Telestration::Point& point : shape.points) points.append(pointToJson(point));
  object.insert(QStringLiteral("points"), points);
  return object;
}

QJsonObject drawingToJson(const Telestration::Drawing& drawing) {
  QJsonObject object;
  object.insert(QStringLiteral("id"), static_cast<qint64>(drawing.id));
  if (drawing.tagId.has_value()) {
    object.insert(QStringLiteral("tagId"), static_cast<qint64>(*drawing.tagId));
  }
  object.insert(QStringLiteral("anchorMs"), static_cast<qint64>(drawing.anchorMs));
  object.insert(QStringLiteral("holdMs"), static_cast<qint64>(drawing.holdMs));
  QJsonArray shapes;
  for (const Telestration::Shape& shape : drawing.shapes) shapes.append(shapeToJson(shape));
  object.insert(QStringLiteral("shapes"), shapes);
  return object;
}

QJsonObject tagToJson(const TagSession::GameTag& tag) {
  QJsonObject object;
  object.insert(QStringLiteral("id"), static_cast<qint64>(tag.id));
  object.insert(QStringLiteral("mainEventId"), static_cast<int>(tag.mainEventId));
  if (!tag.unlistedName.isEmpty()) object.insert(QStringLiteral("unlistedName"), tag.unlistedName);
  QJsonArray followUpIds;
  const int followUpCount = tag.followUpChain().size();
  for (int index = 0; index < followUpCount; ++index) {
    followUpIds.append(static_cast<int>(tag.followUpIds[static_cast<std::size_t>(index)]));
  }
  object.insert(QStringLiteral("followUpIds"), followUpIds);
  object.insert(QStringLiteral("markMs"), static_cast<qint64>(tag.markMs));
  object.insert(QStringLiteral("startMs"), static_cast<qint64>(tag.startMs));
  object.insert(QStringLiteral("endMs"), static_cast<qint64>(tag.endMs));
  if (!tag.note.isEmpty()) object.insert(QStringLiteral("note"), tag.note);
  if (!tag.period.isEmpty()) object.insert(QStringLiteral("period"), tag.period);
  if (!tag.team.isEmpty()) object.insert(QStringLiteral("team"), tag.team);
  object.insert(QStringLiteral("intervalManuallyEdited"), tag.intervalManuallyEdited);
  return object;
}

QJsonObject documentToJson(const Document& document) {
  QJsonObject root;
  root.insert(QStringLiteral("format"), QString::fromLatin1(kFormatName));
  root.insert(QStringLiteral("version"), kFormatVersion);

  QJsonArray sourcePaths;
  for (const QString& path : document.sourcePaths) sourcePaths.append(path);
  root.insert(QStringLiteral("sourcePaths"), sourcePaths);

  QJsonObject game;
  game.insert(QStringLiteral("homeTeamName"), document.homeTeamName);
  game.insert(QStringLiteral("awayTeamName"), document.awayTeamName);
  game.insert(QStringLiteral("homeTeamColor"), document.homeTeamColor);
  game.insert(QStringLiteral("awayTeamColor"), document.awayTeamColor);
  if (document.gameDate.isValid()) {
    game.insert(QStringLiteral("gameDate"), document.gameDate.toString(Qt::ISODate));
  }
  game.insert(QStringLiteral("homeAbbrev"), document.homeAbbrev);
  game.insert(QStringLiteral("awayAbbrev"), document.awayAbbrev);
  root.insert(QStringLiteral("game"), game);

  root.insert(QStringLiteral("matchNote"), document.matchNote);
  root.insert(QStringLiteral("nextTagId"), static_cast<qint64>(document.nextTagId));

  QJsonArray tags;
  for (const TagSession::GameTag& tag : document.tags) tags.append(tagToJson(tag));
  root.insert(QStringLiteral("tags"), tags);
  root.insert(QStringLiteral("labelOverrides"), EventControlsSetup::toJson(document.controls));
  const QJsonArray pathOverrides = EventControlsSetup::pathOverridesToJson(document.controls);
  if (!pathOverrides.isEmpty()) root.insert(QStringLiteral("pathOverrides"), pathOverrides);

  QJsonArray durations;
  for (const EventDefaults::UserOverride& override : document.clipLeadLagOverrides) {
    QJsonObject duration;
    duration.insert(QStringLiteral("eventId"), static_cast<int>(override.eventId));
    duration.insert(QStringLiteral("leadMs"), static_cast<qint64>(override.leadMs));
    duration.insert(QStringLiteral("lagMs"), static_cast<qint64>(override.lagMs));
    durations.append(duration);
  }
  root.insert(QStringLiteral("clipLeadLagOverrides"), durations);

  QJsonObject presentation;
  QJsonArray selectedTagIds;
  for (quint64 tagId : document.presentation.selectedTagIds) {
    selectedTagIds.append(static_cast<qint64>(tagId));
  }
  presentation.insert(QStringLiteral("selectedTagIds"), selectedTagIds);
  presentation.insert(QStringLiteral("currentTagId"), static_cast<qint64>(document.presentation.currentTagId));
  root.insert(QStringLiteral("presentation"), presentation);

  QJsonArray drawings;
  for (const Telestration::Drawing& drawing : document.drawings) drawings.append(drawingToJson(drawing));
  root.insert(QStringLiteral("drawings"), drawings);
  root.insert(QStringLiteral("nextDrawingId"), static_cast<qint64>(document.nextDrawingId));
  return root;
}

bool parseFiniteNumber(const QJsonValue& value, double* number) {
  if (!value.isDouble() || number == nullptr) return false;
  const double raw = value.toDouble();
  if (!std::isfinite(raw)) return false;
  *number = raw;
  return true;
}

bool parseShape(const QJsonValue& value, Telestration::Shape* shape, QString* errorMessage) {
  QJsonObject object;
  if (!requireObject(value, &object, errorMessage, QStringLiteral("A drawing shape"))) return false;

  const QJsonValue kindValue = object.value(QStringLiteral("kind"));
  if (!kindValue.isString() || !shapeKindFromToken(kindValue.toString(), &shape->kind)) {
    *errorMessage = QStringLiteral("A drawing shape has an unknown kind.");
    return false;
  }
  const QJsonValue colorValue = object.value(QStringLiteral("color"));
  if (!colorValue.isString() || !inkColorFromToken(colorValue.toString(), &shape->color)) {
    *errorMessage = QStringLiteral("A drawing shape has an unknown color.");
    return false;
  }

  if (object.contains(QStringLiteral("strokeWidth"))) {
    if (!parseFiniteNumber(object.value(QStringLiteral("strokeWidth")), &shape->strokeWidth) ||
        shape->strokeWidth <= 0.0) {
      *errorMessage = QStringLiteral("A drawing stroke width must be a positive number.");
      return false;
    }
  } else {
    shape->strokeWidth = Telestration::kDefaultStrokeWidth;
  }

  const QJsonValue pointsValue = object.value(QStringLiteral("points"));
  if (!pointsValue.isArray()) {
    *errorMessage = QStringLiteral("A drawing shape needs a points array.");
    return false;
  }
  for (const QJsonValue& pointValue : pointsValue.toArray()) {
    QJsonObject pointObject;
    if (!requireObject(pointValue, &pointObject, errorMessage, QStringLiteral("A drawing point"))) {
      return false;
    }
    Telestration::Point point;
    if (!parseFiniteNumber(pointObject.value(QStringLiteral("x")), &point.x) ||
        !parseFiniteNumber(pointObject.value(QStringLiteral("y")), &point.y)) {
      *errorMessage = QStringLiteral("A drawing point needs finite x and y numbers.");
      return false;
    }
    shape->points.append(point);
  }

  const int minimumPoints = shape->kind == Telestration::ShapeKind::Freehand ? 1 : 2;
  if (shape->points.size() < minimumPoints) {
    *errorMessage = QStringLiteral("A drawing shape does not have enough points.");
    return false;
  }
  return true;
}

bool parseDrawing(const QJsonValue& value, Telestration::Drawing* drawing, QString* errorMessage) {
  QJsonObject object;
  if (!requireObject(value, &object, errorMessage, QStringLiteral("A drawing"))) return false;

  qint64 drawingId = 0;
  if (!requireWholeNumber(object, QStringLiteral("id"), &drawingId, errorMessage) || drawingId <= 0) {
    if (errorMessage->isEmpty()) *errorMessage = QStringLiteral("A drawing id must be a positive integer.");
    return false;
  }
  drawing->id = static_cast<quint64>(drawingId);

  if (object.contains(QStringLiteral("tagId")) && !object.value(QStringLiteral("tagId")).isNull()) {
    qint64 tagId = 0;
    if (!requireWholeNumber(object, QStringLiteral("tagId"), &tagId, errorMessage) || tagId <= 0) {
      if (errorMessage->isEmpty()) *errorMessage = QStringLiteral("A drawing tagId must be a positive integer.");
      return false;
    }
    drawing->tagId = static_cast<quint64>(tagId);
  }

  qint64 anchorMs = 0;
  qint64 holdMs = 0;
  if (!requireWholeNumber(object, QStringLiteral("anchorMs"), &anchorMs, errorMessage) || anchorMs < 0) {
    if (errorMessage->isEmpty()) *errorMessage = QStringLiteral("A drawing anchorMs must be zero or positive.");
    return false;
  }
  if (!requireWholeNumber(object, QStringLiteral("holdMs"), &holdMs, errorMessage) || holdMs <= 0) {
    if (errorMessage->isEmpty()) *errorMessage = QStringLiteral("A drawing holdMs must be a positive integer.");
    return false;
  }
  drawing->anchorMs = anchorMs;
  drawing->holdMs = holdMs;

  const QJsonValue shapesValue = object.value(QStringLiteral("shapes"));
  if (!shapesValue.isArray() || shapesValue.toArray().isEmpty()) {
    *errorMessage = QStringLiteral("A drawing needs at least one shape.");
    return false;
  }
  for (const QJsonValue& shapeValue : shapesValue.toArray()) {
    Telestration::Shape shape;
    if (!parseShape(shapeValue, &shape, errorMessage)) return false;
    drawing->shapes.append(shape);
  }
  return true;
}

bool parseTag(const QJsonValue& value, TagSession::GameTag* tag, QString* errorMessage) {
  QJsonObject object;
  if (!requireObject(value, &object, errorMessage, QStringLiteral("A tag"))) return false;

  qint64 tagId = 0;
  if (!requireWholeNumber(object, QStringLiteral("id"), &tagId, errorMessage) || tagId <= 0) {
    if (errorMessage->isEmpty()) *errorMessage = QStringLiteral("A tag id must be a positive integer.");
    return false;
  }
  tag->id = static_cast<quint64>(tagId);

  qint64 mainEventId = 0;
  if (!requireWholeNumber(object, QStringLiteral("mainEventId"), &mainEventId, errorMessage) ||
      !fitsInInt(mainEventId)) {
    if (errorMessage->isEmpty()) *errorMessage = QStringLiteral("mainEventId must be an integer.");
    return false;
  }
  if (!optionalString(object, QStringLiteral("unlistedName"), &tag->unlistedName, errorMessage)) return false;

  if (mainEventId == static_cast<int>(EventCatalog::EventId::None)) {
    if (tag->unlistedName.trimmed().isEmpty()) {
      *errorMessage = QStringLiteral("A tag with no catalog id needs an unlisted name.");
      return false;
    }
    tag->mainEventId = EventCatalog::EventId::None;
    tag->unlistedName = tag->unlistedName.trimmed();
  } else {
    const auto eventId = static_cast<EventCatalog::EventId>(static_cast<int>(mainEventId));
    const EventCatalog::Node* node = EventCatalog::nodeForId(eventId);
    if (node == nullptr || !node->mainEvent) {
      *errorMessage = QStringLiteral("A tag mainEventId is not a catalog main event.");
      return false;
    }
    if (!tag->unlistedName.isEmpty()) {
      *errorMessage = QStringLiteral("A catalog tag cannot also store an unlisted name.");
      return false;
    }
    tag->mainEventId = eventId;
  }

  const QJsonValue followUpsValue = object.value(QStringLiteral("followUpIds"));
  if (!followUpsValue.isArray()) {
    *errorMessage = QStringLiteral("A tag needs a followUpIds array.");
    return false;
  }
  const QJsonArray followUps = followUpsValue.toArray();
  if (followUps.size() > EventCatalog::kMaxFollowUpDepth) {
    *errorMessage = QStringLiteral("A tag follow-up chain is deeper than the catalog allows.");
    return false;
  }
  TagEvent::FollowUpChain chain;
  chain.count = followUps.size();
  for (int index = 0; index < followUps.size(); ++index) {
    qint64 followUpId = 0;
    if (!Utf8JsonFile::wholeNumber(followUps.at(index), &followUpId) || !fitsInInt(followUpId)) {
      *errorMessage = QStringLiteral("A follow-up id must be an integer.");
      return false;
    }
    const auto eventId = static_cast<EventCatalog::EventId>(static_cast<int>(followUpId));
    const EventCatalog::Node* node = EventCatalog::nodeForId(eventId);
    if (node == nullptr || node->mainEvent) {
      *errorMessage = QStringLiteral("A follow-up id is not a catalog follow-up.");
      return false;
    }
    chain.ids[static_cast<std::size_t>(index)] = eventId;
  }
  tag->setFollowUpChain(chain);

  qint64 markMs = 0;
  qint64 startMs = 0;
  qint64 endMs = 0;
  if (!requireWholeNumber(object, QStringLiteral("markMs"), &markMs, errorMessage) ||
      !requireWholeNumber(object, QStringLiteral("startMs"), &startMs, errorMessage) ||
      !requireWholeNumber(object, QStringLiteral("endMs"), &endMs, errorMessage)) {
    return false;
  }
  tag->markMs = markMs;
  tag->startMs = startMs;
  tag->endMs = endMs;

  if (!optionalString(object, QStringLiteral("note"), &tag->note, errorMessage) ||
      !optionalString(object, QStringLiteral("period"), &tag->period, errorMessage) ||
      !optionalString(object, QStringLiteral("team"), &tag->team, errorMessage)) {
    return false;
  }

  if (object.contains(QStringLiteral("intervalManuallyEdited"))) {
    const QJsonValue edited = object.value(QStringLiteral("intervalManuallyEdited"));
    if (!edited.isBool()) {
      *errorMessage = QStringLiteral("intervalManuallyEdited must be a boolean.");
      return false;
    }
    tag->intervalManuallyEdited = edited.toBool();
  }
  return true;
}

bool parseLeadLag(const QJsonValue& value, EventDefaults::UserOverride* override, QString* errorMessage) {
  QJsonObject object;
  if (!requireObject(value, &object, errorMessage, QStringLiteral("A clip lead/lag override"))) return false;
  qint64 eventId = 0;
  qint64 leadMs = 0;
  qint64 lagMs = 0;
  if (!requireWholeNumber(object, QStringLiteral("eventId"), &eventId, errorMessage) || !fitsInInt(eventId) ||
      !requireWholeNumber(object, QStringLiteral("leadMs"), &leadMs, errorMessage) ||
      !requireWholeNumber(object, QStringLiteral("lagMs"), &lagMs, errorMessage)) {
    if (errorMessage->isEmpty()) *errorMessage = QStringLiteral("eventId must be an integer.");
    return false;
  }
  const auto id = static_cast<EventCatalog::EventId>(static_cast<int>(eventId));
  const EventCatalog::Node* node = EventCatalog::nodeForId(id);
  if (node == nullptr || !node->mainEvent) {
    *errorMessage = QStringLiteral("A clip lead/lag override eventId is not a main event.");
    return false;
  }
  if (leadMs < EventDefaults::kMinLeadLagMs || leadMs > EventDefaults::kMaxLeadLagMs ||
      lagMs < EventDefaults::kMinLeadLagMs || lagMs > EventDefaults::kMaxLeadLagMs) {
    *errorMessage = QStringLiteral("A clip lead/lag override is outside the allowed range.");
    return false;
  }
  override->eventId = id;
  override->leadMs = leadMs;
  override->lagMs = lagMs;
  return true;
}

bool parsePresentation(const QJsonObject& root, PresentationState* presentation, QString* errorMessage) {
  if (!root.contains(QStringLiteral("presentation"))) return true;
  QJsonObject object;
  if (!requireObject(root.value(QStringLiteral("presentation")), &object, errorMessage,
                     QStringLiteral("presentation"))) {
    return false;
  }
  const QJsonValue selected = object.value(QStringLiteral("selectedTagIds"));
  if (!selected.isArray()) {
    *errorMessage = QStringLiteral("presentation.selectedTagIds must be an array.");
    return false;
  }
  for (const QJsonValue& tagIdValue : selected.toArray()) {
    qint64 tagId = 0;
    if (!Utf8JsonFile::wholeNumber(tagIdValue, &tagId) || tagId <= 0) {
      *errorMessage = QStringLiteral("A presentation tag id must be a positive integer.");
      return false;
    }
    presentation->selectedTagIds.append(static_cast<quint64>(tagId));
  }
  if (object.contains(QStringLiteral("currentTagId"))) {
    qint64 currentTagId = 0;
    if (!requireWholeNumber(object, QStringLiteral("currentTagId"), &currentTagId, errorMessage) ||
        currentTagId < 0) {
      if (errorMessage->isEmpty()) {
        *errorMessage = QStringLiteral("presentation.currentTagId must be zero or a positive integer.");
      }
      return false;
    }
    presentation->currentTagId = static_cast<quint64>(currentTagId);
  }
  presentation->specified = true;
  return true;
}

bool parseGame(const QJsonObject& root, Document* document, QString* errorMessage) {
  if (!root.contains(QStringLiteral("game"))) return true;
  QJsonObject game;
  if (!requireObject(root.value(QStringLiteral("game")), &game, errorMessage, QStringLiteral("game"))) {
    return false;
  }
  if (!optionalString(game, QStringLiteral("homeTeamName"), &document->homeTeamName, errorMessage) ||
      !optionalString(game, QStringLiteral("awayTeamName"), &document->awayTeamName, errorMessage) ||
      !optionalString(game, QStringLiteral("homeTeamColor"), &document->homeTeamColor, errorMessage) ||
      !optionalString(game, QStringLiteral("awayTeamColor"), &document->awayTeamColor, errorMessage) ||
      !optionalString(game, QStringLiteral("homeAbbrev"), &document->homeAbbrev, errorMessage) ||
      !optionalString(game, QStringLiteral("awayAbbrev"), &document->awayAbbrev, errorMessage)) {
    return false;
  }
  if (game.contains(QStringLiteral("gameDate"))) {
    const QJsonValue dateValue = game.value(QStringLiteral("gameDate"));
    if (!dateValue.isString()) {
      *errorMessage = QStringLiteral("gameDate must be a YYYY-MM-DD string.");
      return false;
    }
    const QString dateText = dateValue.toString();
    if (!dateText.isEmpty()) {
      const QDate date = QDate::fromString(dateText, Qt::ISODate);
      if (!date.isValid()) {
        *errorMessage = QStringLiteral("gameDate must be a YYYY-MM-DD string.");
        return false;
      }
      document->gameDate = date;
    }
  }
  return true;
}

bool parseSourcePaths(const QJsonObject& root, QStringList* sourcePaths, QString* errorMessage) {
  if (!root.contains(QStringLiteral("sourcePaths"))) return true;
  const QJsonValue value = root.value(QStringLiteral("sourcePaths"));
  if (!value.isArray()) {
    *errorMessage = QStringLiteral("sourcePaths must be an array of strings.");
    return false;
  }
  for (const QJsonValue& pathValue : value.toArray()) {
    if (!pathValue.isString()) {
      *errorMessage = QStringLiteral("sourcePaths must be an array of strings.");
      return false;
    }
    sourcePaths->append(pathValue.toString());
  }
  return true;
}

LoadResult parseDocument(const QJsonDocument& jsonDocument) {
  if (!jsonDocument.isObject()) return failedLoad(LoadStatus::Corrupt, QStringLiteral("the file is not a JSON object."));
  const QJsonObject root = jsonDocument.object();

  const QJsonValue formatValue = root.value(QStringLiteral("format"));
  if (!formatValue.isString() || formatValue.toString() != QLatin1String(kFormatName)) {
    return failedLoad(LoadStatus::Corrupt,
                      QStringLiteral("the format is not \"ava-match-session\"."));
  }

  qint64 version = 0;
  if (!Utf8JsonFile::wholeNumber(root.value(QStringLiteral("version")), &version) || version < 1) {
    return failedLoad(LoadStatus::Corrupt, QStringLiteral("version must be a positive integer."));
  }
  if (version > kFormatVersion) {
    return failedLoad(LoadStatus::NewerVersion,
                      QStringLiteral("version %1 is newer than this version of AVA.").arg(version));
  }

  if (!root.contains(QStringLiteral("tags")) || !root.value(QStringLiteral("tags")).isArray()) {
    return failedLoad(LoadStatus::Corrupt, QStringLiteral("tags must be an array."));
  }

  Document document;
  QString errorMessage;
  if (!parseSourcePaths(root, &document.sourcePaths, &errorMessage) ||
      !parseGame(root, &document, &errorMessage)) {
    return failedLoad(LoadStatus::Corrupt, errorMessage);
  }
  if (!optionalString(root, QStringLiteral("matchNote"), &document.matchNote, &errorMessage)) {
    return failedLoad(LoadStatus::Corrupt, errorMessage);
  }

  QSet<quint64> tagIds;
  for (const QJsonValue& tagValue : root.value(QStringLiteral("tags")).toArray()) {
    TagSession::GameTag tag;
    if (!parseTag(tagValue, &tag, &errorMessage)) return failedLoad(LoadStatus::Corrupt, errorMessage);
    if (tagIds.contains(tag.id)) {
      return failedLoad(LoadStatus::Corrupt, QStringLiteral("two tags share the same id."));
    }
    tagIds.insert(tag.id);
    document.tags.append(tag);
  }

  if (root.contains(QStringLiteral("nextTagId"))) {
    qint64 nextTagId = 0;
    if (!requireWholeNumber(root, QStringLiteral("nextTagId"), &nextTagId, &errorMessage) || nextTagId < 1) {
      if (errorMessage.isEmpty()) errorMessage = QStringLiteral("nextTagId must be a positive integer.");
      return failedLoad(LoadStatus::Corrupt, errorMessage);
    }
    document.nextTagId = static_cast<quint64>(nextTagId);
  }

  if (root.contains(QStringLiteral("labelOverrides")) || root.contains(QStringLiteral("pathOverrides"))) {
    const QJsonValue labelOverrides = root.contains(QStringLiteral("labelOverrides"))
                                          ? root.value(QStringLiteral("labelOverrides"))
                                          : QJsonValue();
    const QJsonValue pathOverrides = root.contains(QStringLiteral("pathOverrides"))
                                         ? root.value(QStringLiteral("pathOverrides"))
                                         : QJsonValue();
    const EventControlsSetup::ParseResult controls =
        EventControlsSetup::fromJson(labelOverrides, pathOverrides);
    if (!controls.ok) return failedLoad(LoadStatus::Corrupt, controls.errorMessage);
    document.controls = controls.setup;
  }

  if (root.contains(QStringLiteral("clipLeadLagOverrides"))) {
    const QJsonValue durationsValue = root.value(QStringLiteral("clipLeadLagOverrides"));
    if (!durationsValue.isArray()) {
      return failedLoad(LoadStatus::Corrupt, QStringLiteral("clipLeadLagOverrides must be an array."));
    }
    QSet<int> seenEventIds;
    for (const QJsonValue& durationValue : durationsValue.toArray()) {
      EventDefaults::UserOverride override;
      if (!parseLeadLag(durationValue, &override, &errorMessage)) {
        return failedLoad(LoadStatus::Corrupt, errorMessage);
      }
      const int eventKey = static_cast<int>(override.eventId);
      if (seenEventIds.contains(eventKey)) {
        return failedLoad(LoadStatus::Corrupt, QStringLiteral("clip lead/lag lists the same event twice."));
      }
      seenEventIds.insert(eventKey);
      document.clipLeadLagOverrides.append(override);
    }
    document.clipLeadLagSpecified = true;
  }

  if (!parsePresentation(root, &document.presentation, &errorMessage)) {
    return failedLoad(LoadStatus::Corrupt, errorMessage);
  }

  if (root.contains(QStringLiteral("drawings"))) {
    const QJsonValue drawingsValue = root.value(QStringLiteral("drawings"));
    if (!drawingsValue.isArray()) {
      return failedLoad(LoadStatus::Corrupt, QStringLiteral("drawings must be an array."));
    }
    QSet<quint64> drawingIds;
    for (const QJsonValue& drawingValue : drawingsValue.toArray()) {
      Telestration::Drawing drawing;
      if (!parseDrawing(drawingValue, &drawing, &errorMessage)) {
        return failedLoad(LoadStatus::Corrupt, errorMessage);
      }
      if (drawingIds.contains(drawing.id)) {
        return failedLoad(LoadStatus::Corrupt, QStringLiteral("two drawings share the same id."));
      }
      drawingIds.insert(drawing.id);
      document.drawings.append(drawing);
    }
  }

  if (root.contains(QStringLiteral("nextDrawingId"))) {
    qint64 nextDrawingId = 0;
    if (!requireWholeNumber(root, QStringLiteral("nextDrawingId"), &nextDrawingId, &errorMessage) ||
        nextDrawingId < 1) {
      if (errorMessage.isEmpty()) errorMessage = QStringLiteral("nextDrawingId must be a positive integer.");
      return failedLoad(LoadStatus::Corrupt, errorMessage);
    }
    document.nextDrawingId = static_cast<quint64>(nextDrawingId);
  }

  LoadResult result;
  result.status = LoadStatus::Loaded;
  result.document = document;
  return result;
}

QString normalizedSourcePath(const QString& path) {
  const QString trimmed = path.trimmed();
  if (trimmed.isEmpty()) return {};
  const QFileInfo info(trimmed);
  const QString canonical = info.canonicalFilePath();
  if (!canonical.isEmpty()) return canonical;
  return info.absoluteFilePath();
}

QString sessionIdentityFileName(const QStringList& normalizedPaths) {
  QByteArray identity;
  for (int index = 0; index < normalizedPaths.size(); ++index) {
    if (index > 0) identity.append('\n');
    identity.append(normalizedPaths.at(index).toUtf8());
  }
  const QByteArray hash = QCryptographicHash::hash(identity, QCryptographicHash::Sha256);
  return QString::fromLatin1(hash.toHex()) + QStringLiteral(".json");
}

}  // namespace

QString applicationDataDirectory() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QStringList normalizedSourcePaths(const QStringList& sourcePaths) {
  QStringList normalized;
  normalized.reserve(sourcePaths.size());
  for (const QString& path : sourcePaths) {
    const QString onePath = normalizedSourcePath(path);
    if (!onePath.isEmpty()) normalized.append(onePath);
  }
  return normalized;
}

QString sessionFilePath(const QString& storageRoot, const QStringList& sourcePaths) {
  const QString root = storageRoot.trimmed();
  const QStringList normalized = normalizedSourcePaths(sourcePaths);
  if (root.isEmpty() || normalized.isEmpty()) return {};
  return QDir(root).filePath(QStringLiteral("sessions/") + sessionIdentityFileName(normalized));
}

QString sessionFilePathForSources(const QStringList& sourcePaths) {
  return sessionFilePath(applicationDataDirectory(), sourcePaths);
}

Document capture(const TagSession& session,
                 const PresentationQueue* presentationQueue,
                 const Telestration::Session* drawings,
                 const QStringList& sourcePaths) {
  Document document;
  document.sourcePaths = normalizedSourcePaths(sourcePaths);
  document.homeTeamName = session.homeTeamName();
  document.awayTeamName = session.awayTeamName();
  document.homeTeamColor = session.homeTeamColor();
  document.awayTeamColor = session.awayTeamColor();
  document.gameDate = session.gameDate();
  document.homeAbbrev = session.homeAbbrev();
  document.awayAbbrev = session.awayAbbrev();
  document.matchNote = session.matchNote();
  document.nextTagId = session.nextTagId();
  document.tags = session.tags();
  document.controls = EventControlsSetup::fromSession(session);
  document.clipLeadLagSpecified = true;
  document.clipLeadLagOverrides = EventDefaults::userOverrides();
  if (presentationQueue != nullptr) {
    document.presentation.specified = true;
    document.presentation.selectedTagIds = presentationQueue->selectedTagIds();
    document.presentation.currentTagId = presentationQueue->currentClipTagId();
  }
  if (drawings != nullptr) {
    document.drawings = drawings->drawings();
    document.nextDrawingId = drawings->nextDrawingId();
  }
  return document;
}

void apply(const Document& document,
           TagSession& session,
           PresentationQueue* presentationQueue,
           Telestration::Session* drawings,
           bool keepMetadata) {
  if (!keepMetadata) {
    session.setGameTeams(document.homeTeamName, document.awayTeamName, document.homeTeamColor,
                         document.awayTeamColor);
    session.setGameMetadata(document.gameDate, document.homeAbbrev, document.awayAbbrev);
  }

  if (EventControlsSetup::applyToSession(session, document.controls) !=
      EventControlsSetup::ApplyStatus::Applied) {
    session.clearEventLabelOverrides();
  }

  session.restoreTags(document.tags, document.nextTagId);
  session.setMatchNote(document.matchNote);

  if (drawings != nullptr) drawings->replaceDrawings(document.drawings, document.nextDrawingId);
  if (presentationQueue != nullptr && document.presentation.specified) {
    presentationQueue->restoreSelection(document.presentation.selectedTagIds,
                                        document.presentation.currentTagId);
  }
  if (document.clipLeadLagSpecified) {
    EventDefaults::replaceUserOverrides(document.clipLeadLagOverrides);
  }
}

LoadResult loadFile(const QString& filePath) {
  const Utf8JsonFile::ReadResult readResult = Utf8JsonFile::read(filePath);
  switch (readResult.status) {
    case Utf8JsonFile::ReadStatus::Ok:
      return parseDocument(readResult.document);
    case Utf8JsonFile::ReadStatus::Missing:
      return failedLoad(LoadStatus::Missing, QStringLiteral("there is no saved session for this match."));
    case Utf8JsonFile::ReadStatus::Empty:
      return failedLoad(LoadStatus::Corrupt, QStringLiteral("the file is empty."));
    case Utf8JsonFile::ReadStatus::ParseError:
      return failedLoad(LoadStatus::Corrupt,
                        QStringLiteral("the file is not valid JSON (%1).").arg(readResult.errorMessage));
    case Utf8JsonFile::ReadStatus::IoError:
      return failedLoad(LoadStatus::IoError, readResult.errorMessage);
  }
  return failedLoad(LoadStatus::IoError, QStringLiteral("the file could not be read."));
}

SaveResult saveFile(const QString& filePath, const Document& document, ExistingFileGuard guard) {
  if (filePath.trimmed().isEmpty()) return failedSave(SaveStatus::IoError, QStringLiteral("no session path."));

  if (guard == ExistingFileGuard::PreserveNewerOrCorrupt && QFile::exists(filePath)) {
    const LoadResult existing = loadFile(filePath);
    if (existing.status == LoadStatus::NewerVersion) {
      return failedSave(SaveStatus::PreservedNewerVersion,
                        QStringLiteral("left the file in place because it is a newer version."));
    }
    if (existing.status == LoadStatus::IoError) return failedSave(SaveStatus::IoError, existing.errorMessage);
    if (existing.status != LoadStatus::Loaded) {
      return failedSave(SaveStatus::PreservedCorrupt,
                        QStringLiteral("left the file in place because it could not be read."));
    }
  }

  QString writeError;
  const QJsonDocument jsonDocument(documentToJson(document));
  if (!Utf8JsonFile::write(filePath, jsonDocument, &writeError)) {
    return failedSave(SaveStatus::IoError, writeError);
  }
  SaveResult result;
  result.status = SaveStatus::Saved;
  return result;
}

LoadResult loadForSources(const QStringList& sourcePaths) {
  const QString filePath = sessionFilePathForSources(sourcePaths);
  if (filePath.isEmpty()) {
    return failedLoad(LoadStatus::Missing, QStringLiteral("there is no source video to identify the match."));
  }
  return loadFile(filePath);
}

SaveResult saveForSources(const QStringList& sourcePaths, const Document& document) {
  const QString filePath = sessionFilePathForSources(sourcePaths);
  if (filePath.isEmpty()) return failedSave(SaveStatus::IoError, QStringLiteral("no session path."));
  return saveFile(filePath, document, ExistingFileGuard::PreserveNewerOrCorrupt);
}

}  // namespace MatchSessionFile
