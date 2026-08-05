#include "cppColorLogger/logger.h"

void refreshCache() {
  LOGGER_LOG(LogLevel::INFO, "Cache refreshed");
}

class Service {
public:
  void start() {
    LOGGER_LOG(LogLevel::INFO, "Service started");
  }
};

int main() {
  LOGGER.setLogLevel(LogLevel::INFO);
  refreshCache();

  Service service;
  service.start();
  return 0;
}
