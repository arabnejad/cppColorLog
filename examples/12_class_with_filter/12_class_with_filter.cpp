#include <cstdio>
#include "cppColorLogger/logger.h"

class Engine {
public:
  void tick() {
    LOGGER_LOG(LOGLEVEL::DEBUG, "Tick operation executed");
    LOGGER_LOG(LOGLEVEL::INFO, "Tick count updated");
    LOGGER_LOG(LOGLEVEL::WARN, "Tick approaching limit");
  }
};

int main() {
  printf("Example 12: Class logging with filtering (only WARN and ERROR shown).\n");
  LOGGER.setLogLevel(LOGLEVEL::DEBUG);
  LOGGER.setFilterLevels({LOGLEVEL::WARN, LOGLEVEL::ERROR});

  Engine e;
  e.tick();

  LOGGER.clearFilterLevels();
  return 0;
}
