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
  QApplication::setStyle(QStyleFactory::create("Fusion"));

  QFont font(QStringLiteral("Inter"));
  if (!QFontInfo(font).exactMatch()) {
    font = QFont(QStringLiteral(".AppleSystemUIFont"));
  }
  font.setPointSize(13);
  font.setHintingPreference(QFont::PreferNoHinting);
  QApplication::setFont(font);

  const QString qssPath = ":/style/theme_light.qss";

  QFile f(qssPath);
  if (!f.exists()) qWarning() << "Theme resource missing:" << f.fileName();


  const QString qss = loadTextFile(qssPath);

  if (qss.isEmpty()) {
    qWarning() << "Failed to load theme QSS:" << qssPath;
    return;
  }

  qApp->setStyleSheet(qss);
}

} // namespace Style