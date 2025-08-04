#include <cstdio>
#include "cppColorLogger/logger.h"

int main() {
  printf("Example 01: Basic usage with INFO log level. DEBUG will be ignored.\n");
  LOGGER.setLogLevel(LOGLEVELL::INFO);
  LOGGER_F(LOGLEVELL::INFO, "This is an info message");
  LOGGER_F(LOGLEVELL::DEBUG, "This debug message will be ignored at INFO level");
  return 0;
}