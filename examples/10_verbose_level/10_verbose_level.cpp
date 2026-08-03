#include <cstdio>
#include "cppColorLogger/logger.h"

int main() {
  printf("Example 10: Demonstrate VERBOSE level logging.\n");
  LOGGER.setLogLevel(LogLevel::VERBOSE);
  LOGGER_F(LogLevel::DEBUG, "Debug is visible");
  LOGGER_F(LogLevel::VERBOSE, "Verbose output is shown too");
  return 0;
}
