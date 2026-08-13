#include <cstdio>
#include "cppColorLogger/logger.h"

class Processor {
public:
  void process() {
    ScopedSettings temporary = LOGGER.scopedSettings();
    LOGGER.setLogLevel(LOGLEVEL::ERROR);
    LOGGER.setFilterLevels({LOGLEVEL::ERROR});

    LOGGER_LOG(LOGLEVEL::INFO, "Filtered out");
    LOGGER_LOG(LOGLEVEL::ERROR, "Logged inside scoped setting");
  }
};

int main() {
  printf("Example 14: Scoped logger settings used inside class methods.\n");
  LOGGER.setLogLevel(LOGLEVEL::INFO);
  Processor p;
  p.process();
  LOGGER_LOG(LOGLEVEL::INFO, "Back to normal settings");
  return 0;
}
