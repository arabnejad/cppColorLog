#include <cstdio>
#include "cppColorLogger/logger.h"

int main() {
  printf("Example 06: Only WARN and ERROR levels will be printed due to filtering.\n");
  LOGGER.setLogLevel(LOGLEVELL::DEBUG);
  LOGGER.setFilterLevels({LOGLEVELL::WARN, LOGLEVELL::ERROR});

  LOGGER_F(LOGLEVELL::DEBUG, "Filtered out debug log");
  LOGGER_F(LOGLEVELL::WARN, "This is a warning");
  LOGGER_F(LOGLEVELL::ERROR, "This is an error");
  return 0;
}