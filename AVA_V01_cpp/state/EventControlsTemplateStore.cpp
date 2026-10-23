#include "EventControlsTemplateStore.h"

#include "MatchSessionFile.h"
#include "Utf8JsonFile.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>

namespace EventControlsTemplateStore {
namespace {

constexpr char kFormatName[] = "ava-control-templates";
constexpr int kMaximumNameLength = 80;

struct StoredTemplate {
  QString name;
  EventControlsSetup::Setup setup;
};

QString templateError(const QString& detail) {
  return QStringLiteral("Control templates: ") + detail;
}

QString storageDirectory(const QString& storageRoot) {
  const QString requested = storageRoot.trimmed();
  if (!requested.isEmpty()) return requested;
  return MatchSessionFile::applicationDataDirectory();
}

QString templateFilePath(const QString& storageRoot) {
  const QString root = storageDirectory(storageRoot);
  if (root.isEmpty()) return {};
  return QDir(root).filePath(QStringLiteral("control-templates.json"));
}

bool nameIsPrintable(const QString& name) {
  for (const QChar character : name) {
    if (character == QLatin1Char('/') || character == QLatin1Char('\\')) return false;
    if (!character.isPrint()) return false;
  }
  return true;
}

enum class NameCheck {
  Ok,
  Empty,
  Builtin,
  Invalid,
};

NameCheck checkName(const QString& name, QString* trimmedName) {
  const QString trimmed = name.trimmed();
  if (trimmedName != nullptr) *trimmedName = trimmed;
  if (trimmed.isEmpty()) return NameCheck::Empty;
  if (EventControlsSetup::isBuiltinDefaultName(trimmed)) return NameCheck::Builtin;
  if (trimmed.size() > kMaximumNameLength || trimmed == QLatin1String(".") ||
      trimmed == QLatin1String("..") || !nameIsPrintable(trimmed)) {
    return NameCheck::Invalid;
  }
  return NameCheck::Ok;
}

ListedTemplate builtinListing() {
  ListedTemplate listing;
  listing.name = EventControlsSetup::builtinDefaultName();
  listing.builtin = true;
  return listing;
}

struct FileContents {
  FileStatus status = FileStatus::Absent;
  QString errorMessage;
  QVector<StoredTemplate> templates;
};

FileContents readTemplateFile(const QString& filePath) {
  FileContents contents;
  if (filePath.isEmpty()) {
    contents.status = FileStatus::IoError;
    contents.errorMessage = templateError(QStringLiteral("no directory is available for templates."));
    return contents;
  }

  const Utf8JsonFile::ReadResult readResult = Utf8JsonFile::read(filePath);
  if (readResult.status == Utf8JsonFile::ReadStatus::Missing) {
    contents.status = FileStatus::Absent;
    return contents;
  }
  if (readResult.status == Utf8JsonFile::ReadStatus::IoError) {
    contents.status = FileStatus::IoError;
    contents.errorMessage = templateError(readResult.errorMessage);
    return contents;
  }
  if (readResult.status != Utf8JsonFile::ReadStatus::Ok || !readResult.document.isObject()) {
    contents.status = FileStatus::Corrupt;
    contents.errorMessage = templateError(
        readResult.errorMessage.isEmpty() ? QStringLiteral("the file is not a JSON object.")
                                          : readResult.errorMessage);
    return contents;
  }

  const QJsonObject root = readResult.document.object();
  const QJsonValue formatValue = root.value(QStringLiteral("format"));
  if (!formatValue.isString() || formatValue.toString() != QLatin1String(kFormatName)) {
    contents.status = FileStatus::Corrupt;
    contents.errorMessage = templateError(QStringLiteral("the format is not \"ava-control-templates\"."));
    return contents;
  }

  qint64 version = 0;
  if (!Utf8JsonFile::wholeNumber(root.value(QStringLiteral("version")), &version) || version < 1) {
    contents.status = FileStatus::Corrupt;
    contents.errorMessage = templateError(QStringLiteral("version must be a positive integer."));
    return contents;
  }
  if (version > kFormatVersion) {
    contents.status = FileStatus::NewerVersion;
    contents.errorMessage =
        templateError(QStringLiteral("version %1 is newer than this version of AVA.").arg(version));
    return contents;
  }

  const QJsonValue templatesValue = root.value(QStringLiteral("templates"));
  if (!templatesValue.isArray()) {
    contents.status = FileStatus::Corrupt;
    contents.errorMessage = templateError(QStringLiteral("templates must be an array."));
    return contents;
  }

  QVector<QString> seenNames;
  for (const QJsonValue& item : templatesValue.toArray()) {
    if (!item.isObject()) {
      contents.status = FileStatus::Corrupt;
      contents.errorMessage = templateError(QStringLiteral("each template must be an object."));
      return contents;
    }
    const QJsonObject object = item.toObject();
    const QJsonValue nameValue = object.value(QStringLiteral("name"));
    if (!nameValue.isString()) {
      contents.status = FileStatus::Corrupt;
      contents.errorMessage = templateError(QStringLiteral("a template name must be a string."));
      return contents;
    }
    QString trimmedName;
    const NameCheck nameCheck = checkName(nameValue.toString(), &trimmedName);
    if (nameCheck != NameCheck::Ok) {
      contents.status = FileStatus::Corrupt;
      contents.errorMessage = templateError(QStringLiteral("a stored template name is not usable."));
      return contents;
    }
    for (const QString& seen : seenNames) {
      if (seen == trimmedName) {
        contents.status = FileStatus::Corrupt;
        contents.errorMessage = templateError(QStringLiteral("two templates share the same name."));
        return contents;
      }
    }
    const QJsonValue pathOverrides = object.contains(QStringLiteral("pathOverrides"))
                                        ? object.value(QStringLiteral("pathOverrides"))
                                        : QJsonValue();
    const EventControlsSetup::ParseResult setup = EventControlsSetup::fromJson(
        object.value(QStringLiteral("labelOverrides")), pathOverrides);
    if (!setup.ok) {
      contents.status = FileStatus::Corrupt;
      contents.errorMessage = templateError(setup.errorMessage);
      return contents;
    }
    seenNames.append(trimmedName);
    StoredTemplate stored;
    stored.name = trimmedName;
    stored.setup = setup.setup;
    contents.templates.append(stored);
  }

  contents.status = FileStatus::Ready;
  return contents;
}

QJsonDocument fileDocument(const QVector<StoredTemplate>& templates) {
  QJsonArray array;
  for (const StoredTemplate& stored : templates) {
    QJsonObject object;
    object.insert(QStringLiteral("name"), stored.name);
    object.insert(QStringLiteral("labelOverrides"), EventControlsSetup::toJson(stored.setup));
    const QJsonArray pathOverrides = EventControlsSetup::pathOverridesToJson(stored.setup);
    if (!pathOverrides.isEmpty()) object.insert(QStringLiteral("pathOverrides"), pathOverrides);
    array.append(object);
  }
  QJsonObject root;
  root.insert(QStringLiteral("format"), QString::fromLatin1(kFormatName));
  root.insert(QStringLiteral("version"), kFormatVersion);
  root.insert(QStringLiteral("templates"), array);
  return QJsonDocument(root);
}

bool writeTemplates(const QString& filePath, const QVector<StoredTemplate>& templates, QString* errorMessage) {
  return Utf8JsonFile::write(filePath, fileDocument(templates), errorMessage);
}

Catalog catalogFromContents(const FileContents& contents) {
  Catalog catalog;
  catalog.fileStatus = contents.status;
  catalog.errorMessage = contents.errorMessage;
  catalog.templates.append(builtinListing());
  QVector<StoredTemplate> userTemplates = contents.templates;
  std::sort(userTemplates.begin(), userTemplates.end(),
            [](const StoredTemplate& left, const StoredTemplate& right) {
              return left.name.compare(right.name, Qt::CaseInsensitive) < 0;
            });
  for (const StoredTemplate& stored : userTemplates) {
    ListedTemplate listing;
    listing.name = stored.name;
    catalog.templates.append(listing);
  }
  return catalog;
}

SaveResult saveFailed(SaveStatus status, const QString& detail) {
  SaveResult result;
  result.status = status;
  result.errorMessage = templateError(detail);
  return result;
}

}  // namespace

Catalog listTemplates(const QString& storageRoot) {
  const FileContents contents = readTemplateFile(templateFilePath(storageRoot));
  if (contents.status == FileStatus::Absent || contents.status == FileStatus::Ready) {
    return catalogFromContents(contents);
  }
  Catalog catalog;
  catalog.fileStatus = contents.status;
  catalog.errorMessage = contents.errorMessage;
  catalog.templates.append(builtinListing());
  return catalog;
}

SaveResult saveTemplate(const QString& name,
                        const EventControlsSetup::Setup& setup,
                        const QString& storageRoot) {
  QString trimmedName;
  switch (checkName(name, &trimmedName)) {
    case NameCheck::Empty:
      return saveFailed(SaveStatus::InvalidName, QStringLiteral("the template name is empty."));
    case NameCheck::Invalid:
      return saveFailed(SaveStatus::InvalidName, QStringLiteral("the template name is not usable."));
    case NameCheck::Builtin:
      return saveFailed(SaveStatus::BuiltinName,
                        QStringLiteral("Camila's layout is the built-in default and cannot be replaced."));
    case NameCheck::Ok:
      break;
  }

  const QString filePath = templateFilePath(storageRoot);
  const FileContents existing = readTemplateFile(filePath);
  if (existing.status == FileStatus::Corrupt) {
    return saveFailed(SaveStatus::Corrupt, QStringLiteral("the template file could not be read, so it was left in place."));
  }
  if (existing.status == FileStatus::NewerVersion) {
    return saveFailed(SaveStatus::NewerVersion,
                      QStringLiteral("the template file is a newer version, so it was left in place."));
  }
  if (existing.status == FileStatus::IoError) {
    SaveResult result;
    result.status = SaveStatus::IoError;
    result.errorMessage = existing.errorMessage;
    return result;
  }

  QVector<StoredTemplate> templates = existing.templates;
  bool replaced = false;
  for (StoredTemplate& stored : templates) {
    if (stored.name != trimmedName) continue;
    stored.setup = setup;
    replaced = true;
    break;
  }
  if (!replaced) {
    StoredTemplate stored;
    stored.name = trimmedName;
    stored.setup = setup;
    templates.append(stored);
  }

  QString writeError;
  if (!writeTemplates(filePath, templates, &writeError)) {
    return saveFailed(SaveStatus::IoError, writeError);
  }
  SaveResult result;
  result.status = SaveStatus::Saved;
  return result;
}

LoadResult loadTemplate(const QString& name, const QString& storageRoot) {
  LoadResult result;
  if (EventControlsSetup::isBuiltinDefaultName(name)) {
    result.status = LoadStatus::Loaded;
    result.setup = EventControlsSetup::builtinDefault();
    return result;
  }

  QString trimmedName;
  if (checkName(name, &trimmedName) != NameCheck::Ok) {
    result.status = LoadStatus::Missing;
    result.errorMessage = templateError(QStringLiteral("there is no template with that name."));
    return result;
  }

  const FileContents contents = readTemplateFile(templateFilePath(storageRoot));
  if (contents.status == FileStatus::Absent) {
    result.status = LoadStatus::Missing;
    result.errorMessage = templateError(QStringLiteral("there is no template with that name."));
    return result;
  }
  if (contents.status == FileStatus::Corrupt) {
    result.status = LoadStatus::Corrupt;
    result.errorMessage = contents.errorMessage;
    return result;
  }
  if (contents.status == FileStatus::NewerVersion) {
    result.status = LoadStatus::NewerVersion;
    result.errorMessage = contents.errorMessage;
    return result;
  }
  if (contents.status == FileStatus::IoError) {
    result.status = LoadStatus::IoError;
    result.errorMessage = contents.errorMessage;
    return result;
  }

  for (const StoredTemplate& stored : contents.templates) {
    if (stored.name != trimmedName) continue;
    result.status = LoadStatus::Loaded;
    result.setup = stored.setup;
    return result;
  }
  result.status = LoadStatus::Missing;
  result.errorMessage = templateError(QStringLiteral("there is no template with that name."));
  return result;
}

RemoveResult removeTemplate(const QString& name, const QString& storageRoot) {
  RemoveResult result;
  if (EventControlsSetup::isBuiltinDefaultName(name)) {
    result.status = RemoveStatus::BuiltinName;
    result.errorMessage =
        templateError(QStringLiteral("Camila's layout is the built-in default and cannot be removed."));
    return result;
  }

  QString trimmedName;
  if (checkName(name, &trimmedName) != NameCheck::Ok) {
    result.status = RemoveStatus::Missing;
    result.errorMessage = templateError(QStringLiteral("there is no template with that name."));
    return result;
  }

  const QString filePath = templateFilePath(storageRoot);
  const FileContents contents = readTemplateFile(filePath);
  if (contents.status == FileStatus::Absent) {
    result.status = RemoveStatus::Missing;
    result.errorMessage = templateError(QStringLiteral("there is no template with that name."));
    return result;
  }
  if (contents.status == FileStatus::Corrupt) {
    result.status = RemoveStatus::Corrupt;
    result.errorMessage = contents.errorMessage;
    return result;
  }
  if (contents.status == FileStatus::NewerVersion) {
    result.status = RemoveStatus::NewerVersion;
    result.errorMessage = contents.errorMessage;
    return result;
  }
  if (contents.status == FileStatus::IoError) {
    result.status = RemoveStatus::IoError;
    result.errorMessage = contents.errorMessage;
    return result;
  }

  QVector<StoredTemplate> remaining;
  bool removed = false;
  for (const StoredTemplate& stored : contents.templates) {
    if (stored.name == trimmedName) {
      removed = true;
      continue;
    }
    remaining.append(stored);
  }
  if (!removed) {
    result.status = RemoveStatus::Missing;
    result.errorMessage = templateError(QStringLiteral("there is no template with that name."));
    return result;
  }

  QString writeError;
  if (!writeTemplates(filePath, remaining, &writeError)) {
    result.status = RemoveStatus::IoError;
    result.errorMessage = templateError(writeError);
    return result;
  }
  result.status = RemoveStatus::Removed;
  return result;
}

}  // namespace EventControlsTemplateStore
