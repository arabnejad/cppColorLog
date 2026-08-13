#include <cstdio>
#include "cppColorLogger/logger.h"

void logTemporarilyWithErrorOnly() {
  ScopedSettings temporary = LOGGER.scopedSettings();
  LOGGER.setLogLevel(LOGLEVEL::ERROR);
  LOGGER.setFilterLevels({LOGLEVEL::ERROR});

  LOGGER_LOG(LOGLEVEL::INFO, "This info log is hidden due to temp settings");
  LOGGER_LOG(LOGLEVEL::ERROR, "This error is visible inside the temporary scope");
}

int main() {
  printf("Example 09: Scoped logger settings for temporary logging behavior.\n");
  LOGGER.setLogLevel(LOGLEVEL::INFO);
  LOGGER_LOG(LOGLEVEL::INFO, "Info before scope");
  logTemporarilyWithErrorOnly();
  LOGGER_LOG(LOGLEVEL::INFO, "Info after scope");
  return 0;
}
