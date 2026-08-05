#include <cstdio>
#include "cppColorLogger/logger.h"

class Engine {
public:
  void tick() {
    LOGGER_LOG(LogLevel::DEBUG, "Tick operation executed");
    LOGGER_LOG(LogLevel::INFO, "Tick count updated");
    LOGGER_LOG(LogLevel::WARN, "Tick approaching limit");
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
