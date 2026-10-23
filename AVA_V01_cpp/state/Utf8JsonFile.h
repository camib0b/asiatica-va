#pragma once

#include <QJsonDocument>
#include <QJsonValue>
#include <QString>

namespace Utf8JsonFile {

enum class ReadStatus {
  Ok,
  Missing,
  IoError,
  Empty,
  ParseError,
};

struct ReadResult {
  ReadStatus status = ReadStatus::Missing;
  QString errorMessage;
  QJsonDocument document;
};

ReadResult read(const QString& filePath);

/// Atomically replaces \p filePath. Creates parent directories. On failure the
/// previous file is left in place.
bool write(const QString& filePath, const QJsonDocument& document, QString* errorMessage = nullptr);

/// True when \p value is a finite JSON number with no fractional part.
bool wholeNumber(const QJsonValue& value, qint64* number);

}  // namespace Utf8JsonFile
