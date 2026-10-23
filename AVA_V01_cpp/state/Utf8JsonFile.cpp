#include "Utf8JsonFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonParseError>
#include <QSaveFile>

#include <cmath>
#include <limits>

namespace Utf8JsonFile {

ReadResult read(const QString& filePath) {
  ReadResult result;
  if (filePath.trimmed().isEmpty()) {
    result.status = ReadStatus::IoError;
    result.errorMessage = QStringLiteral("No file path provided.");
    return result;
  }
  if (!QFile::exists(filePath)) {
    result.status = ReadStatus::Missing;
    result.errorMessage = QStringLiteral("File is missing.");
    return result;
  }

  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    result.status = ReadStatus::IoError;
    result.errorMessage = file.errorString();
    return result;
  }

  const QByteArray bytes = file.readAll();
  if (file.error() != QFileDevice::NoError) {
    result.status = ReadStatus::IoError;
    result.errorMessage = file.errorString();
    return result;
  }
  if (bytes.isEmpty()) {
    result.status = ReadStatus::Empty;
    result.errorMessage = QStringLiteral("File is empty.");
    return result;
  }

  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
  if (parseError.error != QJsonParseError::NoError || document.isNull()) {
    result.status = ReadStatus::ParseError;
    result.errorMessage = parseError.error == QJsonParseError::NoError
                              ? QStringLiteral("File is not a JSON document.")
                              : parseError.errorString();
    return result;
  }

  result.status = ReadStatus::Ok;
  result.document = document;
  return result;
}

bool write(const QString& filePath, const QJsonDocument& document, QString* errorMessage) {
  if (filePath.trimmed().isEmpty()) {
    if (errorMessage) *errorMessage = QStringLiteral("No file path provided.");
    return false;
  }

  const QFileInfo fileInfo(filePath);
  if (!QDir().mkpath(fileInfo.absolutePath())) {
    if (errorMessage) {
      *errorMessage = QStringLiteral("Failed to create the directory for ") + filePath;
    }
    return false;
  }

  const QByteArray bytes = document.toJson(QJsonDocument::Indented);
  QSaveFile file(filePath);
  if (!file.open(QIODevice::WriteOnly)) {
    if (errorMessage) {
      *errorMessage = QStringLiteral("Failed to open file for writing: ") + file.errorString();
    }
    return false;
  }

  const bool deviceWriteFailed =
      file.write(bytes) != bytes.size() || !file.flush() || file.error() != QFileDevice::NoError;
  if (deviceWriteFailed) {
    const QString deviceError = file.errorString();
    file.cancelWriting();
    if (errorMessage) *errorMessage = QStringLiteral("Failed to write file: ") + deviceError;
    return false;
  }

  if (!file.commit()) {
    if (errorMessage) *errorMessage = QStringLiteral("Failed to commit file: ") + file.errorString();
    return false;
  }
  return true;
}

bool wholeNumber(const QJsonValue& value, qint64* number) {
  if (number == nullptr || !value.isDouble()) return false;
  const double raw = value.toDouble();
  if (!std::isfinite(raw)) return false;
  const double truncated = std::trunc(raw);
  if (truncated != raw) return false;
  constexpr double kMinimum = static_cast<double>(std::numeric_limits<qint64>::min());
  constexpr double kMaximum = static_cast<double>(std::numeric_limits<qint64>::max());
  if (truncated < kMinimum || truncated > kMaximum) return false;
  *number = static_cast<qint64>(truncated);
  return true;
}

}  // namespace Utf8JsonFile
