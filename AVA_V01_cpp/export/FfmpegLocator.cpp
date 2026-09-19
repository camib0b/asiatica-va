#include "FfmpegLocator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>

namespace {

bool isRunnableFile(const QString& filePath) {
    if (filePath.isEmpty()) {
        return false;
    }
    const QFileInfo fileInfo(filePath);
    return fileInfo.isFile() && fileInfo.isExecutable();
}

QString bundledHelperPath(const QString& executableName) {
    const QString applicationDirectoryPath = QCoreApplication::applicationDirPath();
    if (applicationDirectoryPath.isEmpty()) {
        return {};
    }
    // Qt reports Contents/MacOS for a bundled app. Helpers live beside MacOS,
    // not next to the executable, so macdeployqt does not treat them as Qt bins.
    return QDir::cleanPath(QDir(applicationDirectoryPath)
                               .filePath(QStringLiteral("../Helpers/%1").arg(executableName)));
}

QStringList wellKnownAbsolutePaths(const QString& executableName) {
    return {
        QDir(QStringLiteral("/opt/homebrew/bin")).filePath(executableName),
        QDir(QStringLiteral("/usr/local/bin")).filePath(executableName),
        QDir(QStringLiteral("/usr/bin")).filePath(executableName),
    };
}

QString findExecutableByName(const QString& executableName) {
    const QString bundledPath = bundledHelperPath(executableName);
    if (isRunnableFile(bundledPath)) {
        return bundledPath;
    }

    for (const QString& candidatePath : wellKnownAbsolutePaths(executableName)) {
        if (isRunnableFile(candidatePath)) {
            return candidatePath;
        }
    }

    return QStandardPaths::findExecutable(executableName);
}

}  // namespace

QString FfmpegLocator::findFfmpeg() {
    return findExecutableByName(QStringLiteral("ffmpeg"));
}

QString FfmpegLocator::findFfprobe() {
    return findExecutableByName(QStringLiteral("ffprobe"));
}
