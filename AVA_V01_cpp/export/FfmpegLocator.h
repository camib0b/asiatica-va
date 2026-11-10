#pragma once

#include <QString>
#include <QStringList>

namespace FfmpegLocator {

/// Coach DMGs contain Contents/Resources/ava-coach-build. Those builds use
/// Contents/Helpers only. A missing helper returns empty: Homebrew and PATH
/// are not searched. Other builds still use a runnable helper when one is
/// present, then the usual developer installs.
QString findFfmpeg();
QString findFfprobe();

/// Empty when this process is not a coach build. Otherwise the helper names
/// that are not executable under Contents/Helpers.
QStringList missingPackagedTools();

/// True when Contents/Resources/ava-coach-build exists beside this MacOS directory.
bool isCoachDistributionBuild(const QString& applicationDirectoryPath);

/// `developerSearchPaths` is used only when the directory is not a coach build
/// and Contents/Helpers does not already contain a runnable binary.
QString resolveExecutable(const QString& executableName,
                          const QString& applicationDirectoryPath,
                          const QStringList& developerSearchPaths);

}  // namespace FfmpegLocator
