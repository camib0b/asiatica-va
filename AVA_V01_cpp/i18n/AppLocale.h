#pragma once

#include <QString>

#include <QtGlobal>

class QSettings;

namespace AppLocale {

/// Separator between compound follow-up path segments (canonical storage and display).
/// Must be a Unicode QString: the arrow is not in Latin-1, so QLatin1StringView would
/// treat the UTF-8 bytes as three separate characters.
inline const QString kCompoundPathSeparator = QStringLiteral(" \u2192 ");

enum class Language {
  English,
  Spanish,
};

Language currentLanguage();
void setLanguage(Language language);
void loadFromSettings();
void saveToSettings();

/// Canonical event token (main event, follow-up segment, or "home"/"away" literals).
QString trEvent(const QString& canonicalToken);

/// Same as trEvent but for an explicit language (used by clip export overlays).
QString trEventForLanguage(const QString& canonicalToken, Language language);

/// Full follow-up chain with " → " separators; each segment translated for display.
QString translateCompoundPath(const QString& canonicalPath);

/// Strips embedded home/away team labels from follow-up paths (for stats/tag columns when team is shown elsewhere).
QString followUpPathWithoutTeamSegments(const QString& followUpEvent, const QString& homeTeamName,
                                        const QString& awayTeamName);

/// One line for tag list / timeline: main + optional compound follow-up.
QString trDisplayTagLine(const QString& mainEvent, const QString& followUpEvent);

/// General UI copy (window chrome, menus, setup, etc.).
QString trUi(const char* key);

} // namespace AppLocale
