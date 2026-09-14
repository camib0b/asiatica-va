#pragma once

#include <QString>

namespace XaiConfig {

/// Returns the xAI API key, or empty if not configured.
QString apiKey();

/// True when an API key is available for Grok requests.
bool isConfigured();

/// Persists an environment API key into the app config folder when missing,
/// so later launches (including from Finder) still work.
void bootstrap();

} // namespace XaiConfig
