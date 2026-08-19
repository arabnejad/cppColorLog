#include <cstdio>
#include <iostream>
#include <memory>

#include "cppColorLogger/logger.h"

int main() {
  std::printf("Sinks: file output, memory output, flushing, and removal.\n");

  LOGGER.clearSinks();
  std::shared_ptr<FileSink> logger_file_sink =
      LOGGER.addFileSink("example.log", FileOpenMode::Truncate, FileFlushMode::Manual);
  std::shared_ptr<InMemorySink> memory_sink = LOGGER.enableInMemorySink();

  const SinkHandle console_handle = LOGGER.addConsoleSink(ConsoleStream::Stderr);

  LOGGER_LOG(LogLevel::Info, "Written to all three sinks");
  LOGGER.removeSink(console_handle);
  LOGGER_LOG(LogLevel::Info, "Written only to the file and memory sinks");

  if (!logger_file_sink->flush())
    std::cerr << logger_file_sink->getLastError() << '\n';

  for (const std::string &entry : memory_sink->getLogs())
    std::cout << "[MEMORY] " << entry << '\n';

  return logger_file_sink->hasError() ? 1 : 0;
}
