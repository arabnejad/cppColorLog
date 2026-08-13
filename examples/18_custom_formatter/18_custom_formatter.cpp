#include <cstdio>
#include <memory>
#include <sstream>

#include "cppColorLogger/logger.h"

class CompactLogFormatter : public LogFormatter {
public:
  std::string formatTimestamp(std::time_t entryTime) const override {
    return formatTimeWithPattern(entryTime, "%H:%M:%S");
  }

  std::string format(const LogEntry &entry) const override {
    std::ostringstream output;
    output << entry.timestamp << ' ' << (entry.level == LOGLEVEL::INFO ? "I" : toString(entry.level)) << ' '
           << entry.function << " | " << entry.message;

    if (!entry.fields.empty()) {
      output << " {";
      for (LogFields::const_iterator field = entry.fields.begin(); field != entry.fields.end(); ++field) {
        if (field != entry.fields.begin())
          output << ' ';
        output << field->first << ':' << field->second;
      }
      output << '}';
    }
    return output.str();
  }
};

void logCompletedRequest() {
  LOGGER_LOG_FIELDS(LOGLEVEL::INFO, "Request completed", {{"status", "200"}, {"duration_ms", "14"}});
}

int main() {
  printf("Example 18: Change formatting without changing sinks.\n");
  LOGGER.setFormatter(std::make_shared<CompactLogFormatter>());
  logCompletedRequest();
  return 0;
}
