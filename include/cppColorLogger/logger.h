#ifndef CPPCOLORLOGGER_LOGGER_H
#define CPPCOLORLOGGER_LOGGER_H

#include <array>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

#if defined(__GNUG__)
#include <cxxabi.h>
#endif

// Internal linkage keeps these header definitions safe across translation units.
namespace Color {
static const char RESET[]   = "\033[0m";
static const char RED[]     = "\033[31m";
static const char GREEN[]   = "\033[32m";
static const char YELLOW[]  = "\033[33m";
static const char BLUE[]    = "\033[34m";
static const char MAGENTA[] = "\033[35m";
static const char CYAN[]    = "\033[36m";
static const char WHITE[]   = "\033[37m";
} // namespace Color

namespace cppcolorlog {

enum class LogLevel { ALWAYS, FATAL, ERROR, WARN, INFO, DEBUG, VERBOSE };

inline const char *toString(LogLevel level) {
  switch (level) {
  case LogLevel::ALWAYS:
    return "ALWAYS";
  case LogLevel::FATAL:
    return "FATAL";
  case LogLevel::ERROR:
    return "ERROR";
  case LogLevel::WARN:
    return "WARN";
  case LogLevel::INFO:
    return "INFO";
  case LogLevel::DEBUG:
    return "DEBUG";
  case LogLevel::VERBOSE:
    return "VERBOSE";
  }
  return "UNKNOWN";
}

inline std::string demangle(const char *name) {
#if defined(__GNUG__)
  int                                     status = 0;
  std::unique_ptr<char, void (*)(void *)> result(abi::__cxa_demangle(name, nullptr, nullptr, &status), std::free);
  return status == 0 && result ? result.get() : name;
#else
  return name;
#endif
}

// Sinks receive the rendered text and the metadata needed for output-specific
// decisions. They never need to parse the formatted message.
struct LogEntry {
  LogLevel    level;
  std::string text;
  std::string color;
};

class LogSink {
public:
  virtual ~LogSink() {}

  virtual bool supportsColor() const {
    return false;
  }

  // This string-only method keeps existing custom sinks source-compatible.
  virtual void write(const std::string &message) = 0;

  // Richer sinks can override this overload to inspect level and color.
  virtual void write(const LogEntry &entry) {
    write(entry.text);
  }
};

class ConsoleSink : public LogSink {
public:
  bool supportsColor() const override {
    return true;
  }

  void write(const std::string &message) override {
    const LogEntry entry = {LogLevel::ALWAYS, message, Color::WHITE};
    write(entry);
  }

  void write(const LogEntry &entry) override {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << entry.color << entry.text << Color::RESET << std::endl;
  }

private:
  std::mutex mutex_;
};

class FileSink : public LogSink {
public:
  explicit FileSink(const std::string &filename) : file_(filename.c_str(), std::ios::app) {}

  void write(const std::string &message) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_)
      file_ << message << std::endl;
  }

  bool isOpen() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return file_.is_open();
  }

private:
  mutable std::mutex mutex_;
  std::ofstream      file_;
};

class InMemorySink : public LogSink {
public:
  void write(const std::string &message) override {
    std::lock_guard<std::mutex> lock(mutex_);
    logs_.push_back(message);
  }

  std::vector<std::string> getLogs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return logs_;
  }

  void clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    logs_.clear();
  }

private:
  std::vector<std::string> logs_;
  mutable std::mutex       mutex_;
};

struct LoggerState {
  LoggerState()
      : logLevel(LogLevel::INFO),
        colors{{Color::WHITE, Color::MAGENTA, Color::RED, Color::YELLOW, Color::GREEN, Color::CYAN, Color::BLUE}} {}

  LogLevel                              logLevel;
  std::set<LogLevel>                    filterLevels;
  std::array<std::string, 7>            colors;
  std::vector<std::shared_ptr<LogSink>> sinks;
  std::shared_ptr<InMemorySink>         inMemorySink;
};

class ScopedSettings;

class Logger {
public:
  // Public construction enables isolated logger instances in tests and tools.
  explicit Logger(bool addConsoleSink = true) {
    if (addConsoleSink)
      state_.sinks.push_back(std::make_shared<ConsoleSink>());
  }

  static Logger &getInstance() {
    static Logger instance;
    return instance;
  }

  Logger(const Logger &)            = delete;
  Logger &operator=(const Logger &) = delete;

  void setLogLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    state_.logLevel = level;
  }

  void setLevelColor(LogLevel level, const std::string &color) {
    const std::size_t index = levelIndex(level);
    if (index >= state_.colors.size())
      return;

    std::lock_guard<std::mutex> lock(stateMutex_);
    state_.colors[index] = color;
  }

  void addSink(const std::shared_ptr<LogSink> &sink) {
    if (!sink)
      return;
    std::lock_guard<std::mutex> lock(stateMutex_);
    state_.sinks.push_back(sink);
  }

  std::shared_ptr<FileSink> addFileSink(const std::string &filename) {
    const std::shared_ptr<FileSink> sink = std::make_shared<FileSink>(filename);
    addSink(sink);
    return sink;
  }

  // Compatibility name: file output has always appended another sink.
  void setFileOutput(const std::string &filename) {
    addFileSink(filename);
  }

  std::shared_ptr<InMemorySink> enableInMemorySink() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!state_.inMemorySink) {
      state_.inMemorySink = std::make_shared<InMemorySink>();
      state_.sinks.push_back(state_.inMemorySink);
    }
    return state_.inMemorySink;
  }

  std::vector<std::string> getInMemoryLogs() const {
    std::shared_ptr<InMemorySink> sink;
    {
      std::lock_guard<std::mutex> lock(stateMutex_);
      sink = state_.inMemorySink;
    }
    return sink ? sink->getLogs() : std::vector<std::string>();
  }

  void setFilterLevels(std::initializer_list<LogLevel> levels) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    state_.filterLevels = std::set<LogLevel>(levels.begin(), levels.end());
  }

  void clearFilterLevels() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    state_.filterLevels.clear();
  }

  void pushLogSetting() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    settingStack_.push_back(state_);
  }

  void popLogSetting() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (settingStack_.empty())
      return;
    state_ = settingStack_.back();
    settingStack_.pop_back();
  }

  ScopedSettings scopedSettings();

  template <typename T>
  void log(LogLevel level, const T &message, const std::string &function = "", const std::string &className = "") {
    std::ostringstream stream;
    stream << message;
    logString(level, stream.str(), function, className);
  }

private:
  static std::size_t levelIndex(LogLevel level) {
    return static_cast<std::size_t>(level);
  }

  static std::tm localTime(std::time_t time) {
    std::tm result = {};
#if defined(_WIN32)
    localtime_s(&result, &time);
#else
    localtime_r(&time, &result);
#endif
    return result;
  }

  static std::string formatLog(LogLevel level, const std::string &message, const std::string &function,
                               const std::string &className) {
    const std::chrono::system_clock::time_point now      = std::chrono::system_clock::now();
    const std::time_t                           time     = std::chrono::system_clock::to_time_t(now);
    const std::tm                               timeInfo = localTime(time);

    char timestamp[32] = {};
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeInfo);

    std::ostringstream stream;
    stream << '[' << timestamp << "] [" << toString(level) << "] ";
    if (!className.empty())
      stream << '[' << className << "::" << function << "] ";
    else
      stream << '[' << function << "] ";
    stream << message;
    return stream.str();
  }

  void logString(LogLevel level, const std::string &message, const std::string &function,
                 const std::string &className) {
    std::vector<std::shared_ptr<LogSink>> sinks;
    std::string                           color = Color::WHITE;

    {
      std::lock_guard<std::mutex> lock(stateMutex_);
      if (level > state_.logLevel)
        return;
      if (!state_.filterLevels.empty() && state_.filterLevels.count(level) == 0)
        return;

      const std::size_t index = levelIndex(level);
      if (index < state_.colors.size())
        color = state_.colors[index];
      sinks = state_.sinks;
    }

    const LogEntry entry = {level, formatLog(level, message, function, className), color};

    // Keep complete entries ordered without holding the configuration lock during I/O.
    std::lock_guard<std::recursive_mutex> outputLock(outputMutex_);
    for (std::vector<std::shared_ptr<LogSink>>::const_iterator sink = sinks.begin(); sink != sinks.end(); ++sink)
      (*sink)->write(entry);
  }

  mutable std::mutex       stateMutex_;
  std::recursive_mutex     outputMutex_;
  LoggerState              state_;
  std::vector<LoggerState> settingStack_;
};

class ScopedSettings {
public:
  explicit ScopedSettings(Logger &logger) : logger_(&logger) {
    logger_->pushLogSetting();
  }

  ~ScopedSettings() {
    if (logger_)
      logger_->popLogSetting();
  }

  ScopedSettings(ScopedSettings &&other) : logger_(other.logger_) {
    other.logger_ = nullptr;
  }

  ScopedSettings(const ScopedSettings &)            = delete;
  ScopedSettings &operator=(const ScopedSettings &) = delete;
  ScopedSettings &operator=(ScopedSettings &&)      = delete;

private:
  Logger *logger_;
};

inline ScopedSettings Logger::scopedSettings() {
  return ScopedSettings(*this);
}

inline Logger &defaultLogger() {
  return Logger::getInstance();
}

} // namespace cppcolorlog

// Compatibility aliases preserve the original public API.
using LogLevel       = cppcolorlog::LogLevel;
using LOGLEVELL      = cppcolorlog::LogLevel;
using LogEntry       = cppcolorlog::LogEntry;
using LogSink        = cppcolorlog::LogSink;
using ConsoleSink    = cppcolorlog::ConsoleSink;
using FileSink       = cppcolorlog::FileSink;
using InMemorySink   = cppcolorlog::InMemorySink;
using LoggerState    = cppcolorlog::LoggerState;
using Logger         = cppcolorlog::Logger;
using LoggerSettings = cppcolorlog::Logger;
using ScopedSettings = cppcolorlog::ScopedSettings;

inline std::string demangle(const char *name) {
  return cppcolorlog::demangle(name);
}

#define LOGGER (::cppcolorlog::defaultLogger())
#define LOGGER_C(level, message)                                                                                       \
  ::cppcolorlog::defaultLogger().log(level, message, __func__, ::cppcolorlog::demangle(typeid(*this).name()))
#define LOGGER_F(level, message) ::cppcolorlog::defaultLogger().log(level, message, __func__)

#endif // CPPCOLORLOGGER_LOGGER_H
