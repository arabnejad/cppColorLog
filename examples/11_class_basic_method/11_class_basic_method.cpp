#include <cstdio>
#include "cppColorLogger/logger.h"

class Service {
public:
  void start() {
    LOGGER_C(LogLevel::INFO, "Service started");
    LOGGER_C(LogLevel::DEBUG, "Internal state initialized");
  }
};

int main() {
  printf("Example 11: Basic class method logging using LOGGER_C.\n");
  LOGGER.setLogLevel(LogLevel::DEBUG);
  Service s;
  s.start();
  return 0;
}
