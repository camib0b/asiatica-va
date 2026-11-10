#pragma once

#include <QString>

namespace AvaVersion {

/// Marketing version, build number, and the git SHA when the build could see
/// the repository. Example: "0.1.0 (1)+0123456789ab". The marketing version and
/// the build number come from AVA_V01_cpp/VERSION.
inline QString current() {
#if defined(AVA_VERSION_STRING)
    return QString::fromUtf8(AVA_VERSION_STRING);
#else
    return QStringLiteral("0.1.0 (1)");
#endif
}

}  // namespace AvaVersion
