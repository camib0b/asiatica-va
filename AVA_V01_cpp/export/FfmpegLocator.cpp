#include "FfmpegLocator.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>

namespace {

constexpr auto kCoachBuildMarkerName = "ava-coach-build";

bool isRunnableFile(const QString& filePath) {
    if (filePath.isEmpty()) {
        return false;
    }
    const QFileInfo fileInfo(filePath);
    return fileInfo.isFile() && fileInfo.isExecutable();
}

QString bundledHelperPath(const QString& applicationDirectoryPath, const QString& executableName) {
    if (applicationDirectoryPath.isEmpty()) {
        return {};
    }
    // Qt reports Contents/MacOS for a bundled app. Helpers live beside MacOS,
    // not next to the executable, so macdeployqt does not treat them as Qt bins.
    return QDir::cleanPath(QDir(applicationDirectoryPath)
                               .filePath(QStringLiteral("../Helpers/%1").arg(executableName)));
}

QString coachBuildMarkerPath(const QString& applicationDirectoryPath) {
    if (applicationDirectoryPath.isEmpty()) {
        return {};
    }
    return QDir::cleanPath(
        QDir(applicationDirectoryPath)
            .filePath(QStringLiteral("../Resources/%1").arg(QLatin1String(kCoachBuildMarkerName))));
}

QStringList wellKnownAbsolutePaths(const QString& executableName) {
    return {
        QDir(QStringLiteral("/opt/homebrew/bin")).filePath(executableName),
        QDir(QStringLiteral("/usr/local/bin")).filePath(executableName),
        QDir(QStringLiteral("/usr/bin")).filePath(executableName),
    };
}

void reportMissingCoachTool(const QString& executableName, const QString& expectedPath) {
    qCritical().noquote()
        << QStringLiteral(
               "Coach build is missing %1 at %2. Homebrew and PATH are not used.")
               .arg(executableName, expectedPath);
}

QString findExecutableByName(const QString& executableName) {
    const QString applicationDirectoryPath = QCoreApplication::applicationDirPath();
    const bool coachDistribution = FfmpegLocator::isCoachDistributionBuild(applicationDirectoryPath);
    QString resolved = FfmpegLocator::resolveExecutable(
        executableName, applicationDirectoryPath, wellKnownAbsolutePaths(executableName));
    if (resolved.isEmpty() && !coachDistribution) {
        resolved = QStandardPaths::findExecutable(executableName);
    }
    if (resolved.isEmpty() && coachDistribution) {
        reportMissingCoachTool(executableName,
                               bundledHelperPath(applicationDirectoryPath, executableName));
    }
    return resolved;
}

}  // namespace

bool FfmpegLocator::isCoachDistributionBuild(const QString& applicationDirectoryPath) {
    const QString markerPath = coachBuildMarkerPath(applicationDirectoryPath);
    return QFileInfo(markerPath).isFile();
}

QString FfmpegLocator::resolveExecutable(const QString& executableName,
                                         const QString& applicationDirectoryPath,
                                         const QStringList& developerSearchPaths) {
    const QString bundledPath = bundledHelperPath(applicationDirectoryPath, executableName);
    if (isRunnableFile(bundledPath)) {
        return bundledPath;
    }
    if (isCoachDistributionBuild(applicationDirectoryPath)) {
        return {};
    }
    for (const QString& candidatePath : developerSearchPaths) {
        if (isRunnableFile(candidatePath)) {
            return candidatePath;
        }
    }
    return {};
}

QStringList FfmpegLocator::missingPackagedTools() {
    const QString applicationDirectoryPath = QCoreApplication::applicationDirPath();
    if (!isCoachDistributionBuild(applicationDirectoryPath)) {
        return {};
    }
    QStringList missingTools;
    const QStringList toolNames = {QStringLiteral("ffmpeg"), QStringLiteral("ffprobe")};
    for (const QString& toolName : toolNames) {
        if (resolveExecutable(toolName, applicationDirectoryPath, {}).isEmpty()) {
            missingTools.append(toolName);
        }
    }
    return missingTools;
}

QString FfmpegLocator::findFfmpeg() {
    return findExecutableByName(QStringLiteral("ffmpeg"));
}

QString FfmpegLocator::findFfprobe() {
    return findExecutableByName(QStringLiteral("ffprobe"));
}
