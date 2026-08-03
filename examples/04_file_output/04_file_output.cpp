#include <cstdio>
#include "cppColorLogger/logger.h"

int main() {
  printf("Example 04: Output logs to a file called 'app.log'.\n");
  LOGGER.setFileOutput("app.log");
  LOGGER.setLogLevel(LogLevel::INFO);
  LOGGER_F(LogLevel::INFO, "This log should be written to the file");
  return 0;
}
