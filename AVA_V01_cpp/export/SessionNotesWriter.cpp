#include "SessionNotesWriter.h"

#include "AppLocale.h"
#include "TagRowDisplay.h"
#include "TagSession.h"

#include <QDir>
#include <QFileDevice>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringList>
#include <QTextDocument>

namespace {

QString teamLabel(const TagSession& session, const QString& teamKey) {
  if (teamKey == QStringLiteral("Home")) {
    return session.homeTeamName();
  }
  if (teamKey == QStringLiteral("Away")) {
    return session.awayTeamName();
  }
  return teamKey;
}

QString clipNoteHeading(const TagSession& session, const TagSession::GameTag& tag) {
  QString heading = TagRowDisplay::formatTimestampMs(tag.markMs);
  const QString eventLine = AppLocale::trDisplayTagLine(tag.mainEvent, tag.followUpEvent);
  if (!eventLine.isEmpty()) {
    heading += QStringLiteral("  ");
    heading += eventLine;
  }
  const QString teamName = teamLabel(session, tag.team);
  if (!teamName.isEmpty()) {
    heading += QStringLiteral("  ");
    heading += teamName;
  }
  return heading;
}

QString notesDocument(const TagSession& session) {
  QStringList lines;
  lines.append(AppLocale::trUi("notes.export_match_heading"));
  lines.append(SessionNotesWriter::matchNotePlainText(session.matchNote()));
  lines.append(QString());
  lines.append(AppLocale::trUi("notes.export_clip_heading"));
  for (const TagSession::GameTag& tag : session.tags()) {
    const QString noteText = tag.note.trimmed();
    if (noteText.isEmpty()) {
      continue;
    }
    lines.append(clipNoteHeading(session, tag));
    lines.append(noteText);
    lines.append(QString());
  }
  return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

}  // namespace

namespace SessionNotesWriter {

QString matchNotePlainText(const QString& html) {
  if (html.trimmed().isEmpty()) {
    return QString();
  }
  QTextDocument document;
  document.setHtml(html);
  return document.toPlainText().trimmed();
}

QString notesPathForXmlReport(const QString& xmlPath) {
  const QFileInfo xmlInfo(xmlPath);
  return QDir(xmlInfo.absolutePath())
      .filePath(xmlInfo.completeBaseName() + QStringLiteral(".notes.txt"));
}

bool writeFile(const TagSession& session, const QString& filePath, QString* errorMessage) {
  if (filePath.trimmed().isEmpty()) {
    if (errorMessage) {
      *errorMessage = AppLocale::trUi("replace_video.notes_no_path");
    }
    return false;
  }

  QSaveFile file(filePath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    if (errorMessage) {
      *errorMessage = AppLocale::trUi("replace_video.notes_open_failed").arg(file.errorString());
    }
    return false;
  }

  const QByteArray bytes = notesDocument(session).toUtf8();
  const bool writeFailed = file.write(bytes) != bytes.size();
  const bool deviceWriteFailed = writeFailed || !file.flush() || file.error() != QFileDevice::NoError;
  if (deviceWriteFailed) {
    const QString deviceError = file.errorString();
    file.cancelWriting();
    if (errorMessage) {
      *errorMessage = AppLocale::trUi("replace_video.notes_write_failed").arg(deviceError);
    }
    return false;
  }
  if (!file.commit()) {
    if (errorMessage) {
      *errorMessage = AppLocale::trUi("replace_video.notes_write_failed").arg(file.errorString());
    }
    return false;
  }
  return true;
}

}  // namespace SessionNotesWriter
