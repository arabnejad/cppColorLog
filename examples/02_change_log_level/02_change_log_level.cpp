#include <cstdio>
#include "cppColorLogger/logger.h"

int main() {
  printf("Example 02: Changing log level to DEBUG to enable lower verbosity.\n");
  LOGGER.setLogLevel(LOGLEVEL::DEBUG);
  LOGGER_LOG(LOGLEVEL::DEBUG, "Debug message now visible");
  LOGGER_LOG(LOGLEVEL::INFO, "Informational log after changing log level");
  return 0;
}
