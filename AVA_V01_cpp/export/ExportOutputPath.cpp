#include "ExportOutputPath.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace ExportOutputPath {

namespace {

QString absoluteDirectoryPath(const QString& directoryPath) {
    const QString trimmed = directoryPath.trimmed();
    if (trimmed.isEmpty()) return {};
    return QDir(trimmed).absolutePath();
}

}  // namespace

bool pathsReferToSameFile(const QString& leftPath, const QString& rightPath) {
    const QString leftTrimmed = leftPath.trimmed();
    const QString rightTrimmed = rightPath.trimmed();
    if (leftTrimmed.isEmpty() || rightTrimmed.isEmpty()) return false;

    const QFileInfo leftInfo(leftTrimmed);
    const QFileInfo rightInfo(rightTrimmed);
    if (leftInfo.absoluteFilePath() == rightInfo.absoluteFilePath()) return true;

    const QString leftCanonical = leftInfo.canonicalFilePath();
    const QString rightCanonical = rightInfo.canonicalFilePath();
    return !leftCanonical.isEmpty() && leftCanonical == rightCanonical;
}

bool pathIsInsideDirectory(const QString& filePath, const QString& directoryPath) {
    const QString directory = absoluteDirectoryPath(directoryPath);
    const QString file = QFileInfo(filePath.trimmed()).absoluteFilePath();
    if (directory.isEmpty() || file.isEmpty()) return false;
    if (file == directory) return true;
    const QString prefix = directory.endsWith(QLatin1Char('/'))
        ? directory
        : directory + QLatin1Char('/');
    return file.startsWith(prefix);
}

QString nonCollidingFilePath(const QString& desiredPath,
                             const std::function<bool(const QString&)>& pathIsTaken) {
    const QString trimmed = desiredPath.trimmed();
    if (trimmed.isEmpty()) return {};
    if (!pathIsTaken || !pathIsTaken(trimmed)) return trimmed;

    const QFileInfo info(trimmed);
    const QString suffix = info.completeSuffix();
    const QString extension = suffix.isEmpty() ? QString() : QStringLiteral(".") + suffix;
    QString stem = info.completeBaseName();
    int nextNumber = 2;
    static const QRegularExpression numberedTail(QStringLiteral("^(.*) \\((\\d+)\\)$"));
    const QRegularExpressionMatch match = numberedTail.match(stem);
    if (match.hasMatch()) {
        stem = match.captured(1);
        nextNumber = match.captured(2).toInt() + 1;
        if (nextNumber < 2) nextNumber = 2;
    }

    constexpr int kMaximumAttempts = 1000;
    const QDir directory(info.absolutePath());
    for (int attempt = 0; attempt < kMaximumAttempts; ++attempt) {
        const QString candidate = directory.filePath(
            QStringLiteral("%1 (%2)%3").arg(stem).arg(nextNumber).arg(extension));
        if (!pathIsTaken(candidate)) return candidate;
        ++nextNumber;
    }
    return {};
}

bool directoryIsNeededByExport(const QString& directoryPath,
                               const QStringList& retainedInputDirectories,
                               const QStringList& sourceVideoPaths) {
    const QString directory = absoluteDirectoryPath(directoryPath);
    if (directory.isEmpty()) return false;

    for (const QString& retained : retainedInputDirectories) {
        if (absoluteDirectoryPath(retained) == directory) return true;
    }
    for (const QString& sourcePath : sourceVideoPaths) {
        if (pathIsInsideDirectory(sourcePath, directory)) return true;
    }
    return false;
}

}  // namespace ExportOutputPath
