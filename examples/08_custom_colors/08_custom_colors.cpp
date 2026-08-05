#include <cstdio>
#include "cppColorLogger/logger.h"

int main() {
  printf("Example 08: Set custom terminal colors for specific log levels.\n");
  LOGGER.setLevelColor(LogLevel::INFO, Color::CYAN);
  LOGGER.setLevelColor(LogLevel::ERROR, Color::MAGENTA);

  LOGGER.setLogLevel(LogLevel::INFO);
  LOGGER_LOG(LogLevel::INFO, "Info message in cyan");
  LOGGER_LOG(LogLevel::ERROR, "Error message in magenta");
  return 0;
}
