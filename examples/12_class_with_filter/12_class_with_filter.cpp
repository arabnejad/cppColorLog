#include <cstdio>
#include "cppColorLogger/logger.h"

class Engine {
public:
  void tick() {
    LOGGER_C(LOGLEVELL::DEBUG, "Tick operation executed");
    LOGGER_C(LOGLEVELL::INFO, "Tick count updated");
    LOGGER_C(LOGLEVELL::WARN, "Tick approaching limit");
  }
};

int main() {
  printf("Example 12: Class logging with filtering (only WARN and ERROR shown).\n");
  LOGGER.setLogLevel(LOGLEVELL::DEBUG);
  LOGGER.setFilterLevels({LOGLEVELL::WARN, LOGLEVELL::ERROR});

  Engine e;
  e.tick();

  LOGGER.clearFilterLevels();
  return 0;
}