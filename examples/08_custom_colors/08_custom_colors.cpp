#include <cstdio>
#include "cppColorLogger/logger.h"

int main() {
  printf("Example 08: Set custom terminal colors for specific log levels.\n");
  LOGGER.setLevelColor(LOGLEVEL::INFO, Color::CYAN);
  LOGGER.setLevelColor(LOGLEVEL::ERROR, Color::MAGENTA);

  LOGGER.setLogLevel(LOGLEVEL::INFO);
  LOGGER_LOG(LOGLEVEL::INFO, "Info message in cyan");
  LOGGER_LOG(LOGLEVEL::ERROR, "Error message in magenta");
  return 0;
}
