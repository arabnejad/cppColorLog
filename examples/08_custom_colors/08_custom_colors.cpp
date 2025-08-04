#include <cstdio>
#include "cppColorLogger/logger.h"

int main() {
  printf("Example 08: Set custom terminal colors for specific log levels.\n");
  LOGGER.setLevelColor(LOGLEVELL::INFO, Color::CYAN);
  LOGGER.setLevelColor(LOGLEVELL::ERROR, Color::MAGENTA);

  LOGGER.setLogLevel(LOGLEVELL::INFO);
  LOGGER_F(LOGLEVELL::INFO, "Info message in cyan");
  LOGGER_F(LOGLEVELL::ERROR, "Error message in magenta");
  return 0;
}