#include "theme.h"

#include <QApplication>
#include <QFile>
#include <QStyleFactory>
#include <QFont>
#include <QFontInfo>
#include <QDebug>

namespace Style {

static QString loadTextFile(const QString& path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
  return QString::fromUtf8(f.readAll());
}

void ApplyLightTheme() {
  const QString qssPath = QStringLiteral(":/style/theme_light.qss");
  const QString qss = loadTextFile(qssPath);
  if (qss.isEmpty()) {
    qWarning() << "Failed to load theme QSS:" << qssPath;
    return;
  }

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