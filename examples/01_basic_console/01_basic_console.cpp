#include <cstdio>
#include "cppColorLogger/logger.h"

int main() {
  printf("Example 01: Basic usage with INFO log level. DEBUG will be ignored.\n");
  LOGGER.setLogLevel(LOGLEVEL::INFO);
  LOGGER_LOG(LOGLEVEL::INFO, "This is an info message");
  LOGGER_LOG(LOGLEVEL::DEBUG, "This debug message will be ignored at INFO level");
  return 0;
}
