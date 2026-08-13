#include <cstdio>
#include "cppColorLogger/logger.h"

int main() {
  printf("Example 06: Only WARN and ERROR levels will be printed due to filtering.\n");
  LOGGER.setLogLevel(LOGLEVEL::DEBUG);
  LOGGER.setFilterLevels({LOGLEVEL::WARN, LOGLEVEL::ERROR});

  LOGGER_LOG(LOGLEVEL::DEBUG, "Filtered out debug log");
  LOGGER_LOG(LOGLEVEL::WARN, "This is a warning");
  LOGGER_LOG(LOGLEVEL::ERROR, "This is an error");
  return 0;
}
