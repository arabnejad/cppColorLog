#include <cstdio>
#include "cppColorLogger/logger.h"

class Calculator {
public:
  int add(int a, int b) {
    LOGGER_C(LogLevel::INFO, "Adding two numbers");
    return a + b;
  }
};

int main() {
  printf("Example 03: Logging from a class method using LOGGER_C.\n");
  LOGGER.setLogLevel(LogLevel::INFO);
  Calculator calc;
  int        result = calc.add(2, 3);
  LOGGER_F(LogLevel::INFO, "Result: " + std::to_string(result));
  return 0;
}
