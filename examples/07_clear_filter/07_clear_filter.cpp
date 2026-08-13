#include <cstdio>
#include "cppColorLogger/logger.h"

int main() {
  printf("Example 07: Clear previously applied log filter levels.\n");
  LOGGER.setLogLevel(LOGLEVEL::DEBUG);
  LOGGER.setFilterLevels({LOGLEVEL::ERROR});
  LOGGER.clearFilterLevels();
  LOGGER_LOG(LOGLEVEL::DEBUG, "Filter cleared, debug log now shown");
  return 0;
}
