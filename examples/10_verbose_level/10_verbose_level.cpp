#include <cstdio>
#include "cppColorLogger/logger.h"

int main() {
  printf("Example 10: Demonstrate VERBOSE level logging.\n");
  LOGGER.setLogLevel(LOGLEVELL::VERBOSE);
  LOGGER_F(LOGLEVELL::DEBUG, "Debug is visible");
  LOGGER_F(LOGLEVELL::VERBOSE, "Verbose output is shown too");
  return 0;
}