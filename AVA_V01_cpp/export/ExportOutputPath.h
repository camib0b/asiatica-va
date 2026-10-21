#pragma once

#include <QString>
#include <QStringList>

#include <functional>

namespace ExportOutputPath {

/// True when both paths name the same file, including when only one exists yet.
bool pathsReferToSameFile(const QString& leftPath, const QString& rightPath);

/// True when \p filePath is \p directoryPath or a file inside it.
bool pathIsInsideDirectory(const QString& filePath, const QString& directoryPath);

/// \p desiredPath when it is free, otherwise "name (2).ext", "name (3).ext", …
/// \p pathIsTaken reports existing files, in-use outputs, and the source video.
/// Empty when no free sibling is found.
QString nonCollidingFilePath(const QString& desiredPath,
                             const std::function<bool(const QString&)>& pathIsTaken);

/// True while an export still reads this directory or listed it as an input.
bool directoryIsNeededByExport(const QString& directoryPath,
                               const QStringList& retainedInputDirectories,
                               const QStringList& sourceVideoPaths);

}  // namespace ExportOutputPath
