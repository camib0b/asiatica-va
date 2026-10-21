#pragma once

#include <QString>
#include <QVector>

namespace TeamAbbreviation {

/// Uppercase alphanumeric runs, treating spaces/punctuation as word breaks.
/// Combining marks are stripped so "Católica" contributes CATOLICA.
inline QVector<QString> alphanumericWords(const QString& teamName) {
  const QString decomposedName = teamName.normalized(QString::NormalizationForm_D);
  QVector<QString> words;
  QString currentWord;
  currentWord.reserve(16);
  for (const QChar character : decomposedName) {
    if (character.isMark()) {
      continue;
    }
    if (character.isLetterOrNumber()) {
      currentWord.append(character.toUpper());
      continue;
    }
    if (!currentWord.isEmpty()) {
      words.append(currentWord);
      currentWord.clear();
    }
  }
  if (!currentWord.isEmpty()) {
    words.append(currentWord);
  }
  return words;
}

/// Up to 3 letters: first letter of each word, then extra letters from the last
/// word so "Old Girls" / "Old Gabs" become OGI / OGA instead of colliding.
inline QString fromTeamName(const QString& teamName) {
  const QVector<QString> words = alphanumericWords(teamName);
  if (words.isEmpty()) {
    return {};
  }

  QString abbreviation;
  abbreviation.reserve(3);
  for (const QString& word : words) {
    abbreviation.append(word.at(0));
    if (abbreviation.size() == 3) {
      return abbreviation;
    }
  }

  for (int wordIndex = words.size() - 1; wordIndex >= 0; --wordIndex) {
    const QString& word = words.at(wordIndex);
    for (int letterIndex = 1; letterIndex < word.size(); ++letterIndex) {
      abbreviation.append(word.at(letterIndex));
      if (abbreviation.size() == 3) {
        return abbreviation;
      }
    }
  }
  return abbreviation;
}

} // namespace TeamAbbreviation
