#include <cstdio>
#include "cppColorLogger/logger.h"

class Processor {
public:
  void process() {
    ScopedSettings temporary = LOGGER.scopedSettings();
    LOGGER.setLogLevel(LogLevel::ERROR);
    LOGGER.setFilterLevels({LogLevel::ERROR});

    LOGGER_LOG(LogLevel::INFO, "Filtered out");
    LOGGER_LOG(LogLevel::ERROR, "Logged inside scoped setting");
  }
};

int main() {
  printf("Example 14: Scoped logger settings used inside class methods.\n");
  LOGGER.setLogLevel(LogLevel::INFO);
  Processor p;
  p.process();
  LOGGER_LOG(LogLevel::INFO, "Back to normal settings");
  return 0;
}
