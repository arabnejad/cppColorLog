#include <cstdio>
#include "cppColorLogger/logger.h"

int main() {
  printf("Example 02: Changing log level to DEBUG to enable lower verbosity.\n");
  LOGGER.setLogLevel(LOGLEVELL::DEBUG);
  LOGGER_F(LOGLEVELL::DEBUG, "Debug message now visible");
  LOGGER_F(LOGLEVELL::INFO, "Informational log after changing log level");
  return 0;
}