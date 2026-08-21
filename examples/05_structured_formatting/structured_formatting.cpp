#include <cstdio>
#include <iomanip>
#include <memory>
#include <sstream>

#include "cppColorLogger/logger.h"

class CompactLogFormatter : public LogFormatter {
public:
  std::string formatTimestamp(const std::chrono::system_clock::time_point &eventTime) const override {
    std::ostringstream output;
    output << formatUtcTimeWithPattern(eventTime, "%H:%M:%S") << '.' << std::setfill('0') << std::setw(3)
           << millisecondsWithinSecond(eventTime) << 'Z';
    return output.str();
  }

  std::string format(const LogEntry &entry) const override {
    std::ostringstream output;
    output << entry.timestamp << ' ' << (entry.level == LogLevel::Info ? "I" : logLevelToString(entry.level)) << ' '
           << entry.sourceFile << ':' << entry.sourceLine << " thread=" << entry.threadId << ' ' << entry.context
           << " | " << entry.message;

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
