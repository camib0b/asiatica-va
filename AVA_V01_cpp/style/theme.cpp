#include "theme.h"

#include <QApplication>
#include <QFile>
#include <QStyleFactory>
#include <QFont>
#include <QFontInfo>
#include <QFileDevice>
#include <QDebug>

namespace {

struct TextFileLoadResult {
    QString content;
    QString errorMessage;
};

TextFileLoadResult loadTextFile(const QString& path) {
    TextFileLoadResult result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.errorMessage = file.errorString();
        return result;
    }

    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        result.errorMessage = file.errorString();
        return result;
    }

    result.content = QString::fromUtf8(bytes);
    if (result.content.isEmpty()) {
        result.errorMessage = QStringLiteral("file is empty");
    }
    return result;
}

}  // namespace

namespace Style {

void ApplyLightTheme() {
  const QString qssPath = QStringLiteral(":/style/theme_light.qss");
  const TextFileLoadResult loaded = loadTextFile(qssPath);
  if (!loaded.errorMessage.isEmpty()) {
    qWarning() << "Failed to load theme QSS:" << qssPath << "-" << loaded.errorMessage;
    return;
  }

  const QString& qss = loaded.content;

  QStyle* style = QStyleFactory::create(QStringLiteral("Fusion"));
  if (!style) {
    style = QStyleFactory::create(QStringLiteral("Windows"));
  }
  if (style) {
    QApplication::setStyle(style);
  } else {
    qWarning() << "Fusion and Windows styles unavailable; using platform default.";
  }

  QFont font(QStringLiteral("Inter"));
  if (!QFontInfo(font).exactMatch()) {
    font = QFont(QStringLiteral(".AppleSystemUIFont"));
  }
  font.setPointSize(13);
  font.setHintingPreference(QFont::PreferNoHinting);
  QApplication::setFont(font);

  qApp->setStyleSheet(qss);
}

} // namespace Style