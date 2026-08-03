#include <cstdio>
#include "cppColorLogger/logger.h"

class Engine {
public:
  void tick() {
    LOGGER_C(LogLevel::DEBUG, "Tick operation executed");
    LOGGER_C(LogLevel::INFO, "Tick count updated");
    LOGGER_C(LogLevel::WARN, "Tick approaching limit");
  }
};

int main() {
  printf("Example 12: Class logging with filtering (only WARN and ERROR shown).\n");
  LOGGER.setLogLevel(LogLevel::DEBUG);
  LOGGER.setFilterLevels({LogLevel::WARN, LogLevel::ERROR});

  Engine e;
  e.tick();

  LOGGER.clearFilterLevels();
  return 0;
}
