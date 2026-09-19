#pragma once

#include <QString>

namespace FfmpegLocator {

/// Resolution order for both binaries:
/// 1. AVA.app/Contents/Helpers/<name> (bundled coach build)
/// 2. Homebrew / usr paths (/opt/homebrew/bin, /usr/local/bin, /usr/bin)
/// 3. QStandardPaths::findExecutable (PATH)
/// Dev builds that are not packaged still find a Homebrew install at step 2.
QString findFfmpeg();
QString findFfprobe();

}  // namespace FfmpegLocator
