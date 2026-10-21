#pragma once

#include <QString>

class TagSession;

namespace SessionNotesWriter {

/// Visible text of a match note. Empty when the HTML has no readable characters.
QString matchNotePlainText(const QString& html);

/// Sibling of an XML report: "match.xml" -> "match.notes.txt".
QString notesPathForXmlReport(const QString& xmlPath);

/// Writes match notes and non-empty clip notes as UTF-8 text.
/// Returns false and sets \p errorMessage when the file cannot be saved.
bool writeFile(const TagSession& session, const QString& filePath, QString* errorMessage = nullptr);

}  // namespace SessionNotesWriter
