#include <cstdio>
#include "cppColorLogger/logger.h"

int main() {
  printf("Example 05: Capture logs in memory using enableInMemorySink.\n");
  LOGGER.enableInMemorySink();
  LOGGER.setLogLevel(LOGLEVELL::INFO);
  LOGGER_F(LOGLEVELL::INFO, "In-memory log: Hello World");

  for (const auto &log : LOGGER.getInMemoryLogs()) {
    std::cout << "[MEMORY] " << log << std::endl;
  }
  return 0;
}