#include <cstdio>
#include "cppColorLogger/logger.h"

class Base {
public:
  virtual void logBase() {
    LOGGER_LOG(LogLevel::INFO, "Base class log");
  }
};

class Derived : public Base {
public:
  void logBase() override {
    LOGGER_LOG(LogLevel::INFO, "Derived override log");
  }
};

int main() {
  printf("Example 13: Automatic context in overridden class methods.\n");
  LOGGER.setLogLevel(LogLevel::INFO);
  Derived derived;
  Base   &object = derived;
  object.logBase();
  return 0;
}
