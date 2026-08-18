#include "cppColorLogger/logger.h"

int main() {
  LOGGER.clearSinks();
  LOGGER.enableInMemorySink();
  LOGGER_LOG(LogLevel::Info, "Installed package works");
  return LOGGER.getInMemoryLogs().size() == 1 ? 0 : 1;
}
