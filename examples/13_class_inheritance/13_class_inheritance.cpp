#include <cstdio>
#include "cppColorLogger/logger.h"

class Base {
public:
  virtual void logBase() {
    LOGGER_C(LOGLEVELL::INFO, "Base class log");
  }
};

class Derived : public Base {
public:
  void logBase() override {
    LOGGER_C(LOGLEVELL::INFO, "Derived override log");
  }
};

int main() {
  printf("Example 13: Inheritance with LOGGER_C in overridden class methods.\n");
  LOGGER.setLogLevel(LOGLEVELL::INFO);
  Base *obj = new Derived();
  obj->logBase();
  delete obj;
  return 0;
}