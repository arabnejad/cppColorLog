#include <cstdio>
#include "cppColorLogger/logger.h"

int main() {
  printf("Example 04: Output logs to a file called 'app.log'.\n");
  LOGGER.setFileOutput("app.log");
  LOGGER.setLogLevel(LOGLEVELL::INFO);
  LOGGER_F(LOGLEVELL::INFO, "This log should be written to the file");
  return 0;
}