#ifndef CPPCOLORLOGGER_LOGGER_H
#define CPPCOLORLOGGER_LOGGER_H

#include <array>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <typeinfo>
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

/**
 * @namespace detail
 * @brief Private helpers that shorten a compiler function signature for logs.
 *
 * Application developers do not call anything in this namespace. Use
 * LOGGER_LOG for automatic context or LOGGER_LOG_WITH_CONTEXT to provide your
 * own context. The logger calls these helpers internally.
 *
 * A compiler describes a method using a long string such as
 * `void Service::start(int)`. The log only needs `Service::start`. The helpers
 * below remove the unwanted pieces in small, testable steps. Template
 * functions need a few extra steps because GCC and Clang place their resolved
 * types at the end of the string.
 *
 * See `docs/source_context_parser.md` for a beginner-friendly walkthrough.
 */
namespace detail {

/**
 * @brief Removes spaces, tabs, and newlines from the two ends of a string.
 *
 * Example: `"  void run()  "` becomes `"void run()"`.
 *
 * @param value The string to clean. It is not modified.
 * @return A new string without surrounding whitespace.
 */
inline std::string trim(const std::string &value) {
  const std::string::size_type first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos)
    return std::string();
  const std::string::size_type last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

/**
 * @brief Checks whether a character can be part of a C++ name.
 *
 * Letters, digits, and `_` return true. This helper is used to recognize where
 * one name ends and the next piece of text begins.
 *
 * @param character The character to check.
 * @return True for a letter, digit, or underscore; otherwise false.
 */
inline bool isIdentifierCharacter(char character) {
  const unsigned char value = static_cast<unsigned char>(character);
  return std::isalnum(value) != 0 || character == '_';
}

/**
 * @brief Checks whether a complete string is one simple C++ name.
 *
 * `T` and `value_type` are accepted. Qualified names and complete types such
 * as `std::string` and `const T` are rejected. The parser only accepts simple
 * names here because it later uses them as safe find-and-replace tokens.
 *
 * @param value The possible identifier.
 * @return True when value starts with a letter or `_` and every remaining
 * character is a letter, digit, or `_`.
 */
inline bool isIdentifier(const std::string &value) {
  if (value.empty() || (!std::isalpha(static_cast<unsigned char>(value[0])) && value[0] != '_'))
    return false;
  for (std::string::size_type index = 1; index < value.size(); ++index) {
    if (!isIdentifierCharacter(value[index]))
      return false;
  }
  return true;
}

/**
 * @brief Splits a compiler's list of resolved template types.
 *
 * GCC separates items with `;`, while Clang commonly uses `,`. A comma inside
 * a type is not a separator. For example:
 *
 * Input:  `U = std::pair<int, int>, T = User`
 * Output: `["U = std::pair<int, int>", "T = User"]`
 *
 * The angle, parenthesis, square-bracket, and brace depth counters tell the
 * function whether a separator is inside a nested C++ expression. Only a
 * separator at depth zero starts a new item.
 *
 * @param bindings The text inside the compiler's final `[with ...]` or `[...]`.
 * @return One string for each top-level template binding.
 */
inline std::vector<std::string> splitTemplateBindings(const std::string &bindings) {
  std::vector<std::string> parts;
  std::string::size_type   start            = 0;
  int                      angleDepth       = 0;
  int                      parenthesisDepth = 0;
  int                      bracketDepth     = 0;
  int                      braceDepth       = 0;

  // Walk from left to right and remember which kind of nested expression is
  // currently open. A comma in `pair<int, int>` must not split the list.
  for (std::string::size_type index = 0; index < bindings.size(); ++index) {
    const char character = bindings[index];
    if (character == '<')
      ++angleDepth;
    else if (character == '>' && angleDepth > 0)
      --angleDepth;
    else if (character == '(')
      ++parenthesisDepth;
    else if (character == ')' && parenthesisDepth > 0)
      --parenthesisDepth;
    else if (character == '[')
      ++bracketDepth;
    else if (character == ']' && bracketDepth > 0)
      --bracketDepth;
    else if (character == '{')
      ++braceDepth;
    else if (character == '}' && braceDepth > 0)
      --braceDepth;

    const bool atTopLevel = angleDepth == 0 && parenthesisDepth == 0 && bracketDepth == 0 && braceDepth == 0;
    if (atTopLevel && (character == ';' || character == ',')) {
      parts.push_back(trim(bindings.substr(start, index - start)));
      start = index + 1;
    }
  }

  parts.push_back(trim(bindings.substr(start)));
  return parts;
}

/**
 * @brief One template name and the real type selected for it.
 *
 * For `template <typename T>` instantiated with `User`, name is `T` and value
 * is `User`.
 */
struct TemplateBinding {
  std::string name;  ///< The placeholder written in the template, for example `T`.
  std::string value; ///< The resolved compiler type, for example `User`.
};

/**
 * @brief Separates the callable signature from its resolved template types.
 *
 * GCC and Clang add template information to the end of a signature:
 *
 * Input signature:  `void parse(T) [with T = int]`
 * Changed signature: `void parse(T)`
 * Return value:      `[{name: "T", value: "int"}]`
 *
 * Clang's shorter `[T = int]` form is handled in the same way. A signature with
 * no valid suffix is left unchanged and produces an empty vector.
 *
 * @param signature The signature to inspect. The recognized suffix is removed
 * from this same string, which is why the parameter is a non-const reference.
 * @return The valid template name/value pairs found in the suffix.
 */
inline std::vector<TemplateBinding> removeTemplateSuffix(std::string &signature) {
  std::vector<TemplateBinding> result;
  const std::string::size_type suffixStart = signature.rfind(" [");
  if (suffixStart == std::string::npos || signature.empty() || signature[signature.size() - 1] != ']')
    return result;

  // Keep only the text between the final square brackets.
  std::string bindings = signature.substr(suffixStart + 2, signature.size() - suffixStart - 3);
  if (bindings.compare(0, 5, "with ") == 0)
    bindings.erase(0, 5);
  if (bindings.find('=') == std::string::npos)
    return result;

  // The caller will parse the function part and template bindings separately.
  signature.erase(suffixStart);
  const std::vector<std::string> parts = splitTemplateBindings(bindings);
  for (std::vector<std::string>::const_iterator part = parts.begin(); part != parts.end(); ++part) {
    const std::string::size_type equals = part->find('=');
    if (equals == std::string::npos)
      continue;

    const std::string name  = trim(part->substr(0, equals));
    const std::string value = trim(part->substr(equals + 1));
    if (isIdentifier(name) && !value.empty())
      result.push_back(TemplateBinding{name, value});
  }
  return result;
}

/**
 * @brief Replaces a template name without changing longer names that contain it.
 *
 * Example: replacing `T` with `User` in `Repository<T>::save` produces
 * `Repository<User>::save`. The `T` at the start of `Type` is not a complete
 * token, so `Type` would remain unchanged.
 *
 * @param text The string to update in place.
 * @param identifier The complete name to find, such as `T`.
 * @param replacement The text that replaces it, such as `User`.
 * @return True if at least one replacement was made.
 */
inline bool replaceIdentifier(std::string &text, const std::string &identifier, const std::string &replacement) {
  bool                   replaced = false;
  std::string::size_type position = 0;
  while ((position = text.find(identifier, position)) != std::string::npos) {
    const bool                   validStart = position == 0 || !isIdentifierCharacter(text[position - 1]);
    const std::string::size_type end        = position + identifier.size();
    const bool                   validEnd   = end == text.size() || !isIdentifierCharacter(text[end]);
    if (validStart && validEnd) {
      text.replace(position, identifier.size(), replacement);
      position += replacement.size();
      replaced = true;
    } else {
      position = end;
    }
  }
  return replaced;
}

/**
 * @brief Finds where a function name starts in the text before its arguments.
 *
 * This helper is mainly needed for operators, whose names can contain spaces.
 * It searches backward from end until it finds a space that belongs to the
 * declaration rather than to a nested template or operator name.
 *
 * Example text: `public: bool __cdecl Value::operator bool`
 * Result points to:                     `Value::operator bool`
 *
 * @param prefix The part of a signature before its function argument list.
 * @param end The position where the backward search starts. For an operator,
 * this is the position of the word `operator`.
 * @return The index of the first character in the qualified callable name.
 */
inline std::string::size_type findNameStart(const std::string &prefix, std::string::size_type end) {
  int  angleDepth       = 0;
  int  parenthesisDepth = 0;
  bool inMsvcQuotedName = false;
  // Search backward because the callable name is the final part of prefix.
  // Depth values prevent spaces inside nested syntax from ending the search.
  for (std::string::size_type index = end; index > 0; --index) {
    const char character = prefix[index - 1];
    if (character == '\'')
      inMsvcQuotedName = true;
    else if (character == '`')
      inMsvcQuotedName = false;
    else if (character == '>')
      ++angleDepth;
    else if (character == '<' && angleDepth > 0)
      --angleDepth;
    else if (character == ')')
      ++parenthesisDepth;
    else if (character == '(' && parenthesisDepth > 0)
      --parenthesisDepth;
    else if ((character == ' ' || character == '\t') && angleDepth == 0 && parenthesisDepth == 0 && !inMsvcQuotedName)
      return index;
  }
  return 0;
}

/**
 * @brief Detects a resolved type that is already present in the class name.
 *
 * Consider `Repository<User>::save() [T = User]`. The type `User` is already
 * visible in `Repository<User>`, so it must not also be appended to the method
 * as `save<User>`.
 *
 * The function searches only between `<` and `>`. When it finds the argument,
 * it replaces those characters with `#` in the working copy. This does not
 * affect the final displayed name; it only prevents the same occurrence from
 * being matched twice.
 *
 * @param classQualifier A temporary copy of the class part, such as
 * `Repository<User>`. It may be marked with `#` characters.
 * @param argument The resolved type to find, such as `User`.
 * @return True if argument was found inside class template brackets.
 */
inline bool consumeClassTemplateArgument(std::string &classQualifier, const std::string &argument) {
  if (classQualifier.find('<') == std::string::npos || argument.empty())
    return false;

  std::string::size_type position = 0;
  while ((position = classQualifier.find(argument, position)) != std::string::npos) {
    // Determine whether this occurrence is between `<` and `>`.
    int angleDepth = 0;
    for (std::string::size_type index = 0; index < position; ++index) {
      if (classQualifier[index] == '<')
        ++angleDepth;
      else if (classQualifier[index] == '>' && angleDepth > 0)
        --angleDepth;
    }
    if (angleDepth > 0) {
      classQualifier.replace(position, argument.size(), argument.size(), '#');
      return true;
    }
    position += argument.size();
  }
  return false;
}

/**
 * @brief Removes everything except the class and callable name.
 *
 * This function handles the shape of the declaration. It does not resolve
 * template placeholders; normalizeFunctionSignature does that later.
 *
 * Input:  `void Service::start(int)`
 * Output: `Service::start`
 *
 * Operators are preserved (`Number::operator()`), and all compiler-specific
 * lambda spellings are simplified to `<lambda>`.
 *
 * @param rawSignature A compiler signature with any `[with ...]` suffix already
 * removed.
 * @param fallbackFunction A simple name, normally from `__func__`, to return if
 * the detailed signature cannot be parsed.
 * @return The qualified callable name. An already-simple signature is returned
 * unchanged; fallbackFunction is used when the input is empty or malformed.
 */
inline std::string extractFunctionName(const std::string &rawSignature, const std::string &fallbackFunction) {
  std::string signature = trim(rawSignature);
  if (signature.empty())
    return fallbackFunction;

  // GCC, Clang, and MSVC spell lambda functions differently. Do not expose an
  // unstable compiler-generated name in the log.
  if (signature.find("lambda") != std::string::npos || signature.find("(anonymous class)") != std::string::npos ||
      signature.find("::$_") != std::string::npos)
    return "<lambda>";

  // The last ')' closes the callable's argument list. Scan backward to find
  // its matching '('; nested parentheses are counted with depth.
  const std::string::size_type parametersEnd = signature.rfind(')');
  if (parametersEnd == std::string::npos)
    return signature;

  int                    depth           = 0;
  std::string::size_type parametersStart = std::string::npos;
  for (std::string::size_type index = parametersEnd + 1; index > 0; --index) {
    const char character = signature[index - 1];
    if (character == ')')
      ++depth;
    else if (character == '(') {
      --depth;
      if (depth == 0) {
        parametersStart = index - 1;
        break;
      }
    }
  }
  if (parametersStart == std::string::npos)
    return fallbackFunction;

  const std::string prefix = trim(signature.substr(0, parametersStart));
  if (prefix.empty())
    return fallbackFunction;

  // An operator name can contain punctuation or spaces, so use the dedicated
  // backward scanner rather than the ordinary last-space rule.
  const std::string::size_type operatorPosition = prefix.rfind("operator");
  if (operatorPosition != std::string::npos)
    return prefix.substr(findNameStart(prefix, operatorPosition));

  // Ordinary functions use the same backward scan, starting at the end.
  return prefix.substr(findNameStart(prefix, prefix.size()));
}

/**
 * @brief Runs the complete parser and returns the context printed in the log.
 *
 * LOGGER_LOG calls this function before passing the resulting context to
 * Logger::log(). It coordinates the smaller helpers in this order:
 *
 * 1. Remove and parse the compiler's template suffix.
 * 2. Extract the class and callable name.
 * 3. Replace class-template placeholders, such as `Repository<T>`.
 * 4. Append unused bindings as function-template arguments.
 *
 * Example:
 * `void Repository<T>::convert(U) [with U = int; T = User]`
 * becomes `Repository<User>::convert<int>`.
 *
 * @param rawSignature The complete string supplied by the compiler.
 * @param fallbackFunction The simple `__func__` name used if parsing fails.
 * @return A short, readable context for the log entry.
 */
inline std::string normalizeFunctionSignature(const std::string &rawSignature,
                                              const std::string &fallbackFunction = std::string()) {
  // Step 1: separate the function declaration from template type information.
  std::string                        signature = trim(rawSignature);
  const std::vector<TemplateBinding> bindings  = removeTemplateSuffix(signature);

  // Step 2: reduce the declaration to a name such as `Service::start`.
  std::string                  function = extractFunctionName(signature, fallbackFunction);
  std::vector<std::string>     functionTemplateArguments;
  const std::string::size_type memberSeparator = function.rfind("::");
  std::string                  classTemplateArguments =
      memberSeparator == std::string::npos ? std::string() : function.substr(0, memberSeparator);

  // Step 3: a binding used in the class name is substituted or marked as
  // already present. Step 4: every unused binding belongs to the function.
  for (std::vector<TemplateBinding>::const_iterator binding = bindings.begin(); binding != bindings.end(); ++binding) {
    if (!replaceIdentifier(function, binding->name, binding->value) &&
        !consumeClassTemplateArgument(classTemplateArguments, binding->value))
      functionTemplateArguments.push_back(binding->value);
  }

  if (!functionTemplateArguments.empty()) {
    function += '<';
    for (std::vector<std::string>::size_type index = 0; index < functionTemplateArguments.size(); ++index) {
      if (index != 0)
        function += ", ";
      function += functionTemplateArguments[index];
    }
    function += '>';
  }

  return function.empty() ? fallbackFunction : function;
}

} // namespace detail

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
      globalState_.sinks.push_back(std::make_shared<ConsoleSink>());
  }

  static Logger &getInstance() {
    static Logger instance;
    return instance;
  }

  Logger(const Logger &)            = delete;
  Logger &operator=(const Logger &) = delete;

  void setLogLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    activeStateLocked(std::this_thread::get_id()).logLevel = level;
  }

  void setLevelColor(LogLevel level, const std::string &color) {
    const std::size_t           index = levelIndex(level);
    std::lock_guard<std::mutex> lock(stateMutex_);
    LoggerState                &state = activeStateLocked(std::this_thread::get_id());
    if (index < state.colors.size())
      state.colors[index] = color;
  }

  void addSink(const std::shared_ptr<LogSink> &sink) {
    if (!sink)
      return;
    std::lock_guard<std::mutex> lock(stateMutex_);
    activeStateLocked(std::this_thread::get_id()).sinks.push_back(sink);
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
    LoggerState                &state = activeStateLocked(std::this_thread::get_id());
    if (!state.inMemorySink) {
      state.inMemorySink = std::make_shared<InMemorySink>();
      state.sinks.push_back(state.inMemorySink);
    }
    return state.inMemorySink;
  }

  std::vector<std::string> getInMemoryLogs() const {
    std::shared_ptr<InMemorySink> sink;
    {
      std::lock_guard<std::mutex> lock(stateMutex_);
      sink = activeStateLocked(std::this_thread::get_id()).inMemorySink;
    }
    return sink ? sink->getLogs() : std::vector<std::string>();
  }

  void setFilterLevels(std::initializer_list<LogLevel> levels) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    activeStateLocked(std::this_thread::get_id()).filterLevels = std::set<LogLevel>(levels.begin(), levels.end());
  }

  void clearFilterLevels() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    activeStateLocked(std::this_thread::get_id()).filterLevels.clear();
  }

  // Create a temporary settings copy for the current thread. If this thread
  // already has a temporary copy, use it as the starting point; otherwise,
  // copy the global settings. Changes then affect only this temporary copy
  // until popLogSetting() restores the previous settings.
  void pushLogSetting() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    const std::thread::id       threadId = std::this_thread::get_id();
    std::vector<LoggerState>   &stack    = scopedStateStacks_[threadId];
    stack.push_back(stack.empty() ? globalState_ : stack.back());
  }

  void popLogSetting() {
    popLogSettingForThread(std::this_thread::get_id());
  }

  ScopedSettings scopedSettings();

  /**
   * @brief Checks whether a log level is currently enabled.
   *
   * Uses the calling thread's active threshold and filter. This only checks the
   * current settings; it does not create or write a message. The returned value
   * is a snapshot and can become outdated if the settings change afterward.
   *
   * @param level The level that a future message would use.
   * @return True when the level passes both the threshold and filter.
   */
  bool isEnabled(LogLevel level) const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return acceptsLevel(activeStateLocked(std::this_thread::get_id()), level);
  }

  template <typename T>
  void log(LogLevel level, const T &message, const std::string &function = "", const std::string &className = "") {
    // Avoid converting a rejected value to text. Expressions passed as
    // `message` have already been evaluated by the caller; use isEnabled()
    // before the call when creating the value itself is expensive.
    std::vector<std::shared_ptr<LogSink>> sinks;
    std::string                           color;
    if (!captureOutputSettings(level, sinks, color))
      return;

    std::ostringstream stream;
    stream << message;
    writeToSinks(level, stream.str(), function, className, color, sinks);
  }

private:
  friend class ScopedSettings;

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

  // The caller must hold stateMutex_. Setters and logging use the current
  // thread's top override when present, otherwise they use the global state.
  LoggerState &activeStateLocked(const std::thread::id &threadId) {
    std::map<std::thread::id, std::vector<LoggerState>>::iterator stack = scopedStateStacks_.find(threadId);
    return stack == scopedStateStacks_.end() || stack->second.empty() ? globalState_ : stack->second.back();
  }

  const LoggerState &activeStateLocked(const std::thread::id &threadId) const {
    std::map<std::thread::id, std::vector<LoggerState>>::const_iterator stack = scopedStateStacks_.find(threadId);
    return stack == scopedStateStacks_.end() || stack->second.empty() ? globalState_ : stack->second.back();
  }

  // Keep the threshold and filter rule in one place for isEnabled() and log().
  static bool acceptsLevel(const LoggerState &state, LogLevel level) {
    return level <= state.logLevel && (state.filterLevels.empty() || state.filterLevels.count(level) != 0);
  }

  void popLogSettingForThread(const std::thread::id &threadId) {
    std::lock_guard<std::mutex>                                   lock(stateMutex_);
    std::map<std::thread::id, std::vector<LoggerState>>::iterator stack = scopedStateStacks_.find(threadId);
    if (stack == scopedStateStacks_.end() || stack->second.empty())
      return;

    stack->second.pop_back();
    if (stack->second.empty())
      scopedStateStacks_.erase(stack);
  }

  // Check the level and copy the selected color and sinks while holding the
  // state lock. Message formatting and output happen after releasing the lock.
  bool captureOutputSettings(LogLevel level, std::vector<std::shared_ptr<LogSink>> &sinks, std::string &color) const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    const LoggerState          &state = activeStateLocked(std::this_thread::get_id());
    if (!acceptsLevel(state, level))
      return false;

    const std::size_t index = levelIndex(level);
    color                   = index < state.colors.size() ? state.colors[index] : Color::WHITE;
    sinks                   = state.sinks;
    return true;
  }

  void writeToSinks(LogLevel level, const std::string &message, const std::string &function,
                    const std::string &className, const std::string &color,
                    const std::vector<std::shared_ptr<LogSink>> &sinks) {
    const LogEntry entry = {level, formatLog(level, message, function, className), color};

    // Keep complete entries ordered without holding the configuration lock during I/O.
    std::lock_guard<std::recursive_mutex> outputLock(outputMutex_);
    for (std::vector<std::shared_ptr<LogSink>>::const_iterator sink = sinks.begin(); sink != sinks.end(); ++sink)
      (*sink)->write(entry);
  }

  mutable std::mutex   stateMutex_;
  std::recursive_mutex outputMutex_;
  LoggerState          globalState_;

  // Each thread owns an independent nested override stack. The map is stored
  // on the logger so a moved ScopedSettings guard can still remove the stack
  // created by its original thread.
  std::map<std::thread::id, std::vector<LoggerState>> scopedStateStacks_;
};

class ScopedSettings {
public:
  explicit ScopedSettings(Logger &logger) : logger_(&logger), ownerThread_(std::this_thread::get_id()) {
    logger_->pushLogSetting();
  }

  ~ScopedSettings() {
    if (logger_)
      logger_->popLogSettingForThread(ownerThread_);
  }

  ScopedSettings(ScopedSettings &&other) : logger_(other.logger_), ownerThread_(other.ownerThread_) {
    other.logger_ = nullptr;
  }

  ScopedSettings(const ScopedSettings &)            = delete;
  ScopedSettings &operator=(const ScopedSettings &) = delete;
  ScopedSettings &operator=(ScopedSettings &&)      = delete;

private:
  Logger *logger_;
  // Remember the creating thread so destruction restores that same stack even
  // if this movable guard is transferred before it is destroyed.
  std::thread::id ownerThread_;
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

/** The shared logger instance used by the convenience macros. */
#define LOGGER (::cppcolorlog::defaultLogger())

// Select the most detailed function description available on this compiler.
// LOGGER_LOG also passes __func__ as a portable fallback.
#if defined(_MSC_VER)
#define CPPCOLORLOG_SIGNATURE __FUNCSIG__
#elif defined(__clang__) || defined(__GNUC__)
#define CPPCOLORLOG_SIGNATURE __PRETTY_FUNCTION__
#else
#define CPPCOLORLOG_SIGNATURE __func__
#endif

/**
 * Logs a message and automatically detects its function or class-method name.
 * Example: LOGGER_LOG(LogLevel::INFO, "Server started");
 */
#define LOGGER_LOG(level, message)                                                                                     \
  ::cppcolorlog::defaultLogger().log(                                                                                  \
      level, message, ::cppcolorlog::detail::normalizeFunctionSignature(CPPCOLORLOG_SIGNATURE, __func__))

/**
 * Logs with a name chosen by the caller. Prefer this for meaningful lambda
 * names or context text that must be identical on every compiler.
 * Example: LOGGER_LOG_WITH_CONTEXT(LogLevel::INFO, "worker", "Task started");
 */
#define LOGGER_LOG_WITH_CONTEXT(level, context, message) ::cppcolorlog::defaultLogger().log(level, message, context)

#define LOGGER_C(level, message)                                                                                       \
  ::cppcolorlog::defaultLogger().log(level, message, __func__, ::cppcolorlog::demangle(typeid(*this).name()))
#define LOGGER_F(level, message) ::cppcolorlog::defaultLogger().log(level, message, __func__)

#endif // CPPCOLORLOGGER_LOGGER_H
