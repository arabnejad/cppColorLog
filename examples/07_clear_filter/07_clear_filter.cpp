#include <cstdio>
#include "cppColorLogger/logger.h"

int main() {
  printf("Example 07: Clear previously applied log filter levels.\n");
  LOGGER.setLogLevel(LogLevel::DEBUG);
  LOGGER.setFilterLevels({LogLevel::ERROR});
  LOGGER.clearFilterLevels();
  LOGGER_F(LogLevel::DEBUG, "Filter cleared, debug log now shown");
  return 0;
}
