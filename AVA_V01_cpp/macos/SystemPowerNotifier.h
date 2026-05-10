#pragma once

/// Callback invoked on the main thread when a system power event fires.
/// `context` is the opaque pointer originally supplied to `avaInstallSystemPowerObservers`.
using AvaSystemPowerCallback = void (*)(void* context);

/// Installs sleep/wake observers tied to `context`. Re-installing for the same context
/// replaces any previously registered callbacks. Pass `nullptr` for a callback you do not need.
///
/// Both callbacks are dispatched on the main thread by NSWorkspace's notification queue,
/// so handlers may freely touch Qt objects that live on the main thread.
void avaInstallSystemPowerObservers(void* context,
                                    AvaSystemPowerCallback onWillSleep,
                                    AvaSystemPowerCallback onDidWake);

/// Removes any sleep/wake observers previously installed for `context`.
/// Safe to call when no observers are registered.
void avaRemoveSystemPowerObservers(void* context);
