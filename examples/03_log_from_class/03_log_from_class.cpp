#include <cstdio>
#include "cppColorLogger/logger.h"

class Calculator {
public:
  int add(int a, int b) {
    LOGGER_LOG(LOGLEVEL::INFO, "Adding two numbers");
    return a + b;
  }
};

int main() {
  printf("Example 03: Automatically detect class-method context with LOGGER_LOG.\n");
  LOGGER.setLogLevel(LOGLEVEL::INFO);
  Calculator calc;
  int        result = calc.add(2, 3);
  LOGGER_LOG(LOGLEVEL::INFO, "Result: " + std::to_string(result));
  return 0;
}
