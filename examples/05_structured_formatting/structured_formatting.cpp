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
    output << entry.timestamp << ' ' << (entry.level == LogLevel::Info ? "I" : logLevelToString(entry.level)) << ' '
           << entry.context << " | " << entry.message;

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
  LOGGER_LOG_FIELDS(LogLevel::Info, "Request completed", {{"status", "200"}, {"duration_ms", "14"}});
}

int main() {
  std::printf("Structured fields and custom formatting.\n");
  LOGGER.setFormatter(std::make_shared<CompactLogFormatter>());
  logCompletedRequest();
  return 0;
}
