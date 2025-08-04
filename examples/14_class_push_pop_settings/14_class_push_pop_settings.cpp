#include <cstdio>
#include "cppColorLogger/logger.h"

class Processor {
public:
  void process() {
    LOGGER.pushLogSetting();
    LOGGER.setLogLevel(LOGLEVELL::ERROR);
    LOGGER.setFilterLevels({LOGLEVELL::ERROR});

    LOGGER_C(LOGLEVELL::INFO, "Filtered out");
    LOGGER_C(LOGLEVELL::ERROR, "Logged inside scoped setting");

    LOGGER.popLogSetting();
  }
};

int main() {
  printf("Example 14: pushLogSetting/popLogSetting used inside class methods.\n");
  LOGGER.setLogLevel(LOGLEVELL::INFO);
  Processor p;
  p.process();
  LOGGER_F(LOGLEVELL::INFO, "Back to normal settings");
  return 0;
}