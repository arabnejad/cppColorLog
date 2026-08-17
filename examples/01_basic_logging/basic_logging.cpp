#include <cstdio>
#include <string>

#include "cppColorLogger/logger.h"

class Calculator {
public:
  int add(int left, int right) {
    LOGGER_LOG(LogLevel::Info, "Adding two numbers");
    return left + right;
  }
};

int main() {
  std::printf("Basic logging: levels and automatic method context.\n");

  LOGGER.setLogLevel(LogLevel::Info);
  LOGGER_LOG(LogLevel::Info, "Application started");
  LOGGER_LOG(LogLevel::Debug, "Hidden at the Info threshold");

  LOGGER.setLogLevel(LogLevel::Verbose);
  LOGGER_LOG(LogLevel::Debug, "Debug is now visible");
  LOGGER_LOG(LogLevel::Verbose, "Verbose is now visible");

  Calculator calculator;
  LOGGER_LOG(LogLevel::Info, "Result: " + std::to_string(calculator.add(2, 3)));
  return 0;
}
