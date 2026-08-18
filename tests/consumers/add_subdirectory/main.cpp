#include "cppColorLogger/logger.h"

int main() {
  LOGGER.clearSinks();
  LOGGER.enableInMemorySink();
  LOGGER_LOG(LogLevel::Info, "add_subdirectory works");
  return LOGGER.getInMemoryLogs().size() == 1 ? 0 : 1;
}
