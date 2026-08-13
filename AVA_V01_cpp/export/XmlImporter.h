#pragma once

#include <QVector>
#include <QString>
#include <QtGlobal>

namespace XmlImporter {

struct ParsedInstance {
  qint64 startMs = 0;
  qint64 endMs = 0;
  QString code;
  QString periodLabel;
  int xmlId = 0;
};

/// Parses a LongoMatch-compatible XML file into a chronologically sorted list of instances.
/// On failure populates \p errorMessage (when non-null) with a human-readable explanation.
bool parse(const QString& filePath,
           QVector<ParsedInstance>* instances,
           QString* errorMessage = nullptr);

/// Returns the sync anchor instance: first Inicio, else first Q1, else earliest instance.
/// Sets \p usedFallback to true when Inicio was not found.
ParsedInstance syncAnchorInstance(const QVector<ParsedInstance>& instances,
                                  bool* usedFallback = nullptr);

} // namespace XmlImporter
