#include <cstdio>

#include "cppColorLogger/logger.h"

void logCompletedRequest() {
  LOGGER_LOG_FIELDS(LOGLEVEL::INFO, "Request completed", {{"status", "200"}, {"duration_ms", "14"}});
}

int main() {
  printf("Example 17: Attach structured fields to a log message.\n");
  LOGGER.setLogLevel(LOGLEVEL::INFO);
  logCompletedRequest();
  return 0;
}
