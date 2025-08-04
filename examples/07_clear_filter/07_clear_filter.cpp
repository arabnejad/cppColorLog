#include <cstdio>
#include "cppColorLogger/logger.h"

int main() {
  printf("Example 07: Clear previously applied log filter levels.\n");
  LOGGER.setLogLevel(LOGLEVELL::DEBUG);
  LOGGER.setFilterLevels({LOGLEVELL::ERROR});
  LOGGER.clearFilterLevels();
  LOGGER_F(LOGLEVELL::DEBUG, "Filter cleared, debug log now shown");
  return 0;
}