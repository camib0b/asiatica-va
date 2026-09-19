#pragma once

#include <QString>

namespace XaiConfig {

/// Returns the xAI API key, or empty if not configured.
QString apiKey();

/// True when an API key is available for Grok requests.
bool isConfigured();

/// Chat-completions model for game metadata inference.
/// Override with AVA_XAI_CHAT_MODEL or XAI_CHAT_MODEL (review default periodically).
QString chatModelName();

/// Maximum JPEG thumbnail size sent to the vision endpoint (bytes).
/// Override with AVA_XAI_MAX_THUMBNAIL_BYTES (review default periodically).
int maxMetadataThumbnailBytes();

/// Persists an environment API key into the app config folder when missing,
/// so later launches (including from Finder) still work.
void bootstrap();

} // namespace XaiConfig
