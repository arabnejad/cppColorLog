#include <cstdio>
#include "cppColorLogger/logger.h"

int main() {
  printf("Example 10: Demonstrate VERBOSE level logging.\n");
  LOGGER.setLogLevel(LOGLEVEL::VERBOSE);
  LOGGER_LOG(LOGLEVEL::DEBUG, "Debug is visible");
  LOGGER_LOG(LOGLEVEL::VERBOSE, "Verbose output is shown too");
  return 0;
}
