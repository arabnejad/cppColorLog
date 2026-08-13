#include <cstdio>
#include "cppColorLogger/logger.h"

int main() {
  printf("Example 05: Capture logs in memory using enableInMemorySink.\n");
  LOGGER.enableInMemorySink();
  LOGGER.setLogLevel(LOGLEVEL::INFO);
  LOGGER_LOG(LOGLEVEL::INFO, "In-memory log: Hello World");

  for (const auto &log : LOGGER.getInMemoryLogs()) {
    std::cout << "[MEMORY] " << log << std::endl;
  }
  return 0;
}
