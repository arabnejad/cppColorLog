#include <cstdio>
#include "cppColorLogger/logger.h"

int main() {
  printf("Example 01: Basic usage with INFO log level. DEBUG will be ignored.\n");
  LOGGER.setLogLevel(LogLevel::INFO);
  LOGGER_F(LogLevel::INFO, "This is an info message");
  LOGGER_F(LogLevel::DEBUG, "This debug message will be ignored at INFO level");
  return 0;
}
