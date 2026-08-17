#include <cstdio>

#include "cppColorLogger/logger.h"

void logWithTemporarySettings() {
  ScopedSettings temporary = LOGGER.scopedSettings();
  LOGGER.setLogLevel(LogLevel::Error);
  LOGGER.setAllowedLevels({LogLevel::Error});

  LOGGER_LOG(LogLevel::Info, "Hidden inside the temporary settings");
  LOGGER_LOG(LogLevel::Error, "Visible inside the temporary settings");
}

int main() {
  std::printf("Settings: allowed levels, colors, and temporary overrides.\n");

  LOGGER.setLogLevel(LogLevel::Debug);
  LOGGER.setAllowedLevels({LogLevel::Warn, LogLevel::Error});
  LOGGER_LOG(LogLevel::Debug, "Hidden by the allowed-level list");
  LOGGER_LOG(LogLevel::Warn, "Warning is allowed");

  LOGGER.clearAllowedLevels();
  LOGGER.setLevelColor(LogLevel::Info, Color::Cyan);
  LOGGER.setColorMode(ColorMode::Automatic);
  LOGGER_LOG(LogLevel::Info, "The allowed-level list has been cleared");

  logWithTemporarySettings();
  LOGGER_LOG(LogLevel::Info, "The previous settings were restored");
  return 0;
}
