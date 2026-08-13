#include "cppColorLogger/logger.h"

void refreshCache() {
  LOGGER_LOG(LOGLEVEL::INFO, "Cache refreshed");
}

class Service {
public:
  void start() {
    LOGGER_LOG(LOGLEVEL::INFO, "Service started");
  }
};

int main() {
  LOGGER.setLogLevel(LOGLEVEL::INFO);
  refreshCache();

  Service service;
  service.start();
  return 0;
}
