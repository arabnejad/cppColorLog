#include <cstdio>
#include "cppColorLogger/logger.h"

void logTemporarilyWithErrorOnly() {
  LOGGER.pushLogSetting();
  LOGGER.setLogLevel(LOGLEVELL::ERROR);
  LOGGER.setFilterLevels({LOGLEVELL::ERROR});

  LOGGER_F(LOGLEVELL::INFO, "This info log is hidden due to temp settings");
  LOGGER_F(LOGLEVELL::ERROR, "This error is visible inside push scope");
  LOGGER.popLogSetting();
}

int main() {
  printf("Example 09: Push and pop logger settings for temporary logging behavior.\n");
  LOGGER.setLogLevel(LOGLEVELL::INFO);
  LOGGER_F(LOGLEVELL::INFO, "Info before scope");
  logTemporarilyWithErrorOnly();
  LOGGER_F(LOGLEVELL::INFO, "Info after scope");
  return 0;
}