#include <cstdio>
#include "cppColorLogger/logger.h"

void logTemporarilyWithErrorOnly() {
  LOGGER.pushLogSetting();
  LOGGER.setLogLevel(LogLevel::ERROR);
  LOGGER.setFilterLevels({LogLevel::ERROR});

  LOGGER_LOG(LogLevel::INFO, "This info log is hidden due to temp settings");
  LOGGER_LOG(LogLevel::ERROR, "This error is visible inside push scope");
  LOGGER.popLogSetting();
}

int main() {
  printf("Example 09: Push and pop logger settings for temporary logging behavior.\n");
  LOGGER.setLogLevel(LogLevel::INFO);
  LOGGER_LOG(LogLevel::INFO, "Info before scope");
  logTemporarilyWithErrorOnly();
  LOGGER_LOG(LogLevel::INFO, "Info after scope");
  return 0;
}
