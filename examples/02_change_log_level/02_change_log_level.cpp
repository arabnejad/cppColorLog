#include <cstdio>
#include "cppColorLogger/logger.h"

int main() {
  printf("Example 02: Changing log level to DEBUG to enable lower verbosity.\n");
  LOGGER.setLogLevel(LogLevel::DEBUG);
  LOGGER_LOG(LogLevel::DEBUG, "Debug message now visible");
  LOGGER_LOG(LogLevel::INFO, "Informational log after changing log level");
  return 0;
}
