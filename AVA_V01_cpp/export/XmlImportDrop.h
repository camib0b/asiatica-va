#pragma once

#include <QFileInfo>
#include <QList>
#include <QMimeData>
#include <QString>
#include <QUrl>

#include <optional>

namespace XmlImportDrop {

enum class Decision {
  /// Not one local .xml file. The widget under the cursor may handle it.
  Unrelated,
  /// One .xml file while drop import is unavailable.
  Reject,
  /// One .xml file and the session can start the import.
  Accept,
};

struct Offer {
  Decision decision = Decision::Unrelated;
  QString filePath;
};

/// Drop import is the empty-session shortcut. The settings menu stays available
/// after the first tag.
inline bool sessionAllowsDrop(bool videoOpen, int tagCount) {
  return videoOpen && tagCount == 0;
}

/// Exactly one local file whose suffix is .xml. Directories and other payloads
/// are not an XML drop.
inline std::optional<QString> singleXmlFilePath(const QMimeData* mimeData) {
  if (mimeData == nullptr || !mimeData->hasUrls()) {
    return std::nullopt;
  }
  const QList<QUrl> urls = mimeData->urls();
  if (urls.size() != 1 || !urls.first().isLocalFile()) {
    return std::nullopt;
  }
  const QFileInfo fileInfo(urls.first().toLocalFile());
  if (!fileInfo.isFile()) {
    return std::nullopt;
  }
  if (fileInfo.suffix().compare(QStringLiteral("xml"), Qt::CaseInsensitive) != 0) {
    return std::nullopt;
  }
  return fileInfo.absoluteFilePath();
}

inline Offer classify(const QMimeData* mimeData, bool sessionAllows) {
  const std::optional<QString> filePath = singleXmlFilePath(mimeData);
  if (!filePath.has_value()) {
    return {};
  }
  Offer offer;
  offer.filePath = *filePath;
  offer.decision = sessionAllows ? Decision::Accept : Decision::Reject;
  return offer;
}

}  // namespace XmlImportDrop
