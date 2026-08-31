#pragma once

#include <QString>

namespace LicenseConfig {

/// Base URL of the license Worker, no trailing slash. Empty means local-only mode.
QString apiUrl();

bool isApiConfigured();

void bootstrap();

} // namespace LicenseConfig
