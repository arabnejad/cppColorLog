#include <cstdio>
#include "cppColorLogger/logger.h"

int main() {
  printf("Example 06: Only WARN and ERROR levels will be printed due to filtering.\n");
  LOGGER.setLogLevel(LogLevel::DEBUG);
  LOGGER.setFilterLevels({LogLevel::WARN, LogLevel::ERROR});

  LOGGER_LOG(LogLevel::DEBUG, "Filtered out debug log");
  LOGGER_LOG(LogLevel::WARN, "This is a warning");
  LOGGER_LOG(LogLevel::ERROR, "This is an error");
  return 0;
}
