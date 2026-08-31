#pragma once

#include <QString>

/// Stable per-Mac identifier used to bind trials and licenses.
///
/// macOS: `IOPlatformUUID` from IOKit (`IOPlatformExpertDevice`), prefixed with `mac-`.
/// That UUID stays the same across app reinstalls and user accounts on the same hardware.
/// It changes if Apple replaces the logic board.
///
/// Linux (dev builds): SHA-256 of `/etc/machine-id`, prefixed with `linux-`.
///
/// If the platform id cannot be read, a random id is created once and stored in app data
/// (`device-id.txt`). Wiping that file starts a new identity.
QString avaStableDeviceId();
