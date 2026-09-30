#pragma once

#include <QString>

namespace AvaVersion {

/// Bundle version, plus the git SHA when the build could see the repository.
inline QString current() {
#if defined(AVA_VERSION_STRING)
    return QString::fromUtf8(AVA_VERSION_STRING);
#else
    return QStringLiteral("0.1.0");
#endif
}

}  // namespace AvaVersion
