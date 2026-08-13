#include <cstdio>
#include "cppColorLogger/logger.h"

int main() {
  printf("Example 04: Output logs to a file called 'app.log'.\n");
  LOGGER.addFileSink("app.log");
  LOGGER.setLogLevel(LOGLEVEL::INFO);
  LOGGER_LOG(LOGLEVEL::INFO, "This log should be written to the file");
  return 0;
}
