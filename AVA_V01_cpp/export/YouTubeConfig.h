#pragma once

#include <QString>

namespace YouTubeConfig {

/// Returns the Google OAuth client ID, or empty if not configured.
QString clientId();

/// Returns the YouTube Data API key, or empty if not configured.
QString apiKey();

/// True when a client ID is available for the OAuth flow.
bool isConfigured();

/// Copies bundled credentials into the app config folder when missing.
void bootstrap();

/// Human-readable setup hint when credentials are missing.
QString setupInstructions();

} // namespace YouTubeConfig
