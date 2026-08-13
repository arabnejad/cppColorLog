#include <cstdio>
#include "cppColorLogger/logger.h"

class Base {
public:
  virtual void logBase() {
    LOGGER_LOG(LOGLEVEL::INFO, "Base class log");
  }
};

class Derived : public Base {
public:
  void logBase() override {
    LOGGER_LOG(LOGLEVEL::INFO, "Derived override log");
  }
};

int main() {
  printf("Example 13: Automatic context in overridden class methods.\n");
  LOGGER.setLogLevel(LOGLEVEL::INFO);
  Derived derived;
  Base   &object = derived;
  object.logBase();
  return 0;
}
