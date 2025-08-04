
#ifndef CPPCOLORLOG_H
#define CPPCOLORLOG_H

#include <iostream>
#include <fstream>
#include <chrono>
#include <sstream>
#include <mutex>
#include <map>
#include <memory>
#include <typeinfo>
#include <vector>
#include <atomic>
#include <set>
#include <cxxabi.h>
#include <cstdlib>

// Demangle for readable class names
// Demangles a C++ type name to a human-readable class name
inline std::string demangle(const char *name) {
  int         status    = 0;
  char       *demangled = abi::__cxa_demangle(name, nullptr, nullptr, &status);
  std::string result    = (status == 0 && demangled) ? demangled : name;
  std::free(demangled);
  return result;
}

// ANSI color codes for terminal output
namespace Color {
const char *RESET   = "\033[0m";
const char *RED     = "\033[31m";
const char *GREEN   = "\033[32m";
const char *YELLOW  = "\033[33m";
const char *BLUE    = "\033[34m";
const char *MAGENTA = "\033[35m";
const char *CYAN    = "\033[36m";
const char *WHITE   = "\033[37m";
} // namespace Color

// Log level enumeration from ALWAYS to VERBOSE
enum class LOGLEVELL { ALWAYS, FATAL, ERROR, WARN, INFO, DEBUG, VERBOSE };

// Abstract base class for log output targets (sinks)
class LogSink {
public:
  virtual ~LogSink() {}
  virtual void write(const std::string &message) = 0;
  virtual bool supportsColor() const {
    return false;
  }
};

// Log sink that writes to std::cout with color support
class ConsoleSink : public LogSink {
public:
  bool supportsColor() const override {
    return true;
  }

public:
  void write(const std::string &message) override {
    std::string levelTag;
    size_t      start = message.find("] [");
    if (start != std::string::npos) {
      size_t levelStart = start + 3;
      size_t levelEnd   = message.find("]", levelStart);
      if (levelEnd != std::string::npos) {
        levelTag = message.substr(levelStart, levelEnd - levelStart);
      }
    }

    const char *color = Color::WHITE;
    if (levelTag == "FATAL")
      color = Color::MAGENTA;
    else if (levelTag == "ERROR")
      color = Color::RED;
    else if (levelTag == "WARN")
      color = Color::YELLOW;
    else if (levelTag == "INFO")
      color = Color::GREEN;
    else if (levelTag == "DEBUG")
      color = Color::CYAN;
    else if (levelTag == "VERBOSE")
      color = Color::BLUE;

    std::cout << color << message << Color::RESET << std::endl;
  }
};

// Log sink that writes plain messages to a file (no color)
class FileSink : public LogSink {
public:
  explicit FileSink(const std::string &filename) : filename_(filename) {}
  void write(const std::string &message) override {
    std::ofstream file(filename_, std::ios::app);
    file << message << std::endl;
  }

private:
  std::string filename_;
};

// Log sink that stores logs in memory (for testing/debug)
class InMemorySink : public LogSink {
public:
  void write(const std::string &message) override {
    std::lock_guard<std::mutex> lock(mutex_);
    logs_.push_back(message);
  }

  const std::vector<std::string> &getLogs() const {
    return logs_;
  }

private:
  std::vector<std::string> logs_;
  mutable std::mutex       mutex_;
};

// Snapshot of logger configuration for push/pop functionality
struct LoggerState {
  LOGLEVELL                             logLevel;
  std::set<LOGLEVELL>                   filterLevels;
  std::map<LOGLEVELL, const char *>     colorMap;
  std::vector<std::shared_ptr<LogSink>> sinks;
};

// Core logger class (singleton). All logs go through here.
class Logger {
  friend class LoggerSettings;

public:
  // Returns the singleton instance of the logger
  static Logger &getInstance() {
    static Logger instance;
    return instance;
  }

  template <typename T>
  // Logs a message if level passes current and filter checks
  void log(LOGLEVELL level, const T &message, const std::string &func = "", const std::string &className = "") {
    if (level > currentLevel.load())
      return;
    std::lock_guard<std::mutex> lock(filterMutex_);
    if (!filterLevels_.empty() && filterLevels_.find(level) == filterLevels_.end())
      return;

    // Builds the full log message string with timestamp and context
    std::string logMessage = formatLog(level, message, func, className);
    // Sends a formatted log message to all active sinks
    writeLog(logMessage);
  }

private:
  Logger() : currentLevel(LOGLEVELL::INFO) {
    colorMap_[LOGLEVELL::ALWAYS]  = Color::WHITE;
    colorMap_[LOGLEVELL::FATAL]   = Color::MAGENTA;
    colorMap_[LOGLEVELL::ERROR]   = Color::RED;
    colorMap_[LOGLEVELL::WARN]    = Color::YELLOW;
    colorMap_[LOGLEVELL::INFO]    = Color::GREEN;
    colorMap_[LOGLEVELL::DEBUG]   = Color::CYAN;
    colorMap_[LOGLEVELL::VERBOSE] = Color::BLUE;

    sinks_.push_back(std::make_shared<ConsoleSink>());
  }

  ~Logger() {}

  // Sends a formatted log message to all active sinks
  void writeLog(const std::string &msg) {
    std::lock_guard<std::mutex> lock(sinksMutex_);
    for (auto &sink : sinks_) {
      sink->write(msg);
    }
  }

  // Builds the full log message string with timestamp and context
  std::string formatLog(LOGLEVELL level, const std::string &message, const std::string &func,
                        const std::string &className) {
    std::stringstream ss;
    auto              now      = std::chrono::system_clock::now();
    auto              time     = std::chrono::system_clock::to_time_t(now);
    std::tm          *timeinfo = std::localtime(&time);

    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);

    ss << "[" << buffer << "] ";
    // Converts enum log level to readable string
    ss << "[" << logLevelToString(level) << "] ";
    if (!className.empty())
      ss << "[" << className << "::" << func << "] ";
    else
      ss << "[" << func << "] ";
    ss << message;

    return ss.str();
  }

  // Converts enum log level to readable string
  std::string logLevelToString(LOGLEVELL level) {
    switch (level) {
    case LOGLEVELL::ALWAYS:
      return "ALWAYS";
    case LOGLEVELL::FATAL:
      return "FATAL";
    case LOGLEVELL::ERROR:
      return "ERROR";
    case LOGLEVELL::WARN:
      return "WARN";
    case LOGLEVELL::INFO:
      return "INFO";
    case LOGLEVELL::DEBUG:
      return "DEBUG";
    case LOGLEVELL::VERBOSE:
      return "VERBOSE";
    default:
      return "UNKNOWN";
    }
  }

  // Saves current logger configuration to the stack
  void pushLogSetting() {
    std::lock_guard<std::mutex> lock(sinksMutex_);
    LoggerState                 state;
    state.logLevel     = currentLevel.load();
    state.filterLevels = filterLevels_;
    state.colorMap     = colorMap_;
    state.sinks        = sinks_;
    settingStack_.push_back(std::move(state));
  }

  // Restores the most recent saved logger configuration
  void popLogSetting() {
    std::lock_guard<std::mutex> lock(sinksMutex_);
    if (settingStack_.empty())
      return;
    LoggerState state = settingStack_.back();
    settingStack_.pop_back();

    currentLevel.store(state.logLevel);
    filterLevels_ = state.filterLevels;
    colorMap_     = state.colorMap;
    sinks_        = state.sinks;
  }

  std::atomic<LOGLEVELL>                currentLevel;
  std::set<LOGLEVELL>                   filterLevels_;
  std::map<LOGLEVELL, const char *>     colorMap_;
  std::vector<std::shared_ptr<LogSink>> sinks_;
  std::shared_ptr<InMemorySink>         inMemorySink_;
  std::vector<LoggerState>              settingStack_;
  std::vector<std::string>              emptyLogs_;
  std::mutex                            sinksMutex_, filterMutex_, colorMapMutex_;
};

// Public-facing API for users to configure the logger
class LoggerSettings {
public:
  // Set the minimum log level to be displayed
  void setLogLevel(LOGLEVELL level) {
    Logger::getInstance().currentLevel.store(level);
  }

  // Override the default ANSI color for a specific log level
  void setLevelColor(LOGLEVELL level, const char *color) {
    std::lock_guard<std::mutex> lock(Logger::getInstance().colorMapMutex_);
    Logger::getInstance().colorMap_[level] = color;
  }

  // Append a file sink for logging to disk
  void setFileOutput(const std::string &filename) {
    Logger::getInstance().sinks_.push_back(std::make_shared<FileSink>(filename));
  }

  // Activate in-memory logging for programmatic access
  void enableInMemorySink() {
    if (!Logger::getInstance().inMemorySink_) {
      Logger::getInstance().inMemorySink_ = std::make_shared<InMemorySink>();
      Logger::getInstance().sinks_.push_back(Logger::getInstance().inMemorySink_);
    }
  }

  // Access logs collected in the in-memory sink
  const std::vector<std::string> &getInMemoryLogs() const {
    if (Logger::getInstance().inMemorySink_)
      return Logger::getInstance().inMemorySink_->getLogs();
    return Logger::getInstance().emptyLogs_;
  }

  // Whitelist specific levels that should be logged
  void setFilterLevels(std::initializer_list<LOGLEVELL> levels) {
    std::lock_guard<std::mutex> lock(Logger::getInstance().filterMutex_);
    Logger::getInstance().filterLevels_.clear();
    Logger::getInstance().filterLevels_.insert(levels.begin(), levels.end());
  }

  // Clear any level filtering to show all levels that pass threshold
  void clearFilterLevels() {
    std::lock_guard<std::mutex> lock(Logger::getInstance().filterMutex_);
    Logger::getInstance().filterLevels_.clear();
  }

  // Saves current logger configuration to the stack
  void pushLogSetting() {
    // Saves current logger configuration to the stack
    Logger::getInstance().pushLogSetting();
  }

  // Restores the most recent saved logger configuration
  void popLogSetting() {
    // Restores the most recent saved logger configuration
    Logger::getInstance().popLogSetting();
  }
};

static LoggerSettings LOGGER;

namespace detail {
inline void logHelper(LOGLEVELL level, const std::string &msg, const std::string &func, const std::string &className) {
  Logger::getInstance().log(level, msg, func, className);
}

inline void logHelper(LOGLEVELL level, const std::string &msg, const std::string &func) {
  Logger::getInstance().log(level, msg, func, "");
}
} // namespace detail

// Macro to log from within a class (includes class name)
#define LOGGER_C(level, msg) ::detail::logHelper(level, msg, __FUNCTION__, demangle(typeid(*this).name()))
// Macro to log from a free function (no class context)
#define LOGGER_F(level, msg) ::detail::logHelper(level, msg, __FUNCTION__)

#endif // CPPCOLORLOG_H