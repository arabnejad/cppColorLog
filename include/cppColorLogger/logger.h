#ifndef CPPCOLORLOGGER_LOGGER_H
#define CPPCOLORLOGGER_LOGGER_H

#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

// Internal linkage keeps these header definitions safe across translation units.
namespace Color {
static const char Reset[]   = "\033[0m";
static const char Red[]     = "\033[31m";
static const char Green[]   = "\033[32m";
static const char Yellow[]  = "\033[33m";
static const char Blue[]    = "\033[34m";
static const char Magenta[] = "\033[35m";
static const char Cyan[]    = "\033[36m";
static const char White[]   = "\033[37m";
} // namespace Color

enum class LogLevel { Always, Fatal, Error, Warn, Info, Debug, Verbose };

inline const char *logLevelToString(LogLevel level) {
  switch (level) {
  case LogLevel::Always:
    return "ALWAYS";
  case LogLevel::Fatal:
    return "FATAL";
  case LogLevel::Error:
    return "ERROR";
  case LogLevel::Warn:
    return "WARN";
  case LogLevel::Info:
    return "INFO";
  case LogLevel::Debug:
    return "DEBUG";
  case LogLevel::Verbose:
    return "VERBOSE";
  }
  return "UNKNOWN";
}

// Automatic is the safe default: use color for a supported interactive terminal,
// but not for redirected output or when NO_COLOR is set. Enabled always emits
// ANSI colors, and Disabled always emits plain text.
enum class ColorMode { Automatic, Enabled, Disabled };

// Stdout is the default destination for console logs. Stderr keeps diagnostic
// logs separate from normal program output such as generated data.
enum class ConsoleStream { Stdout, Stderr };

// Append preserves existing file contents. Truncate clears the file when the
// sink opens it.
enum class FileOpenMode { Append, Truncate };

// AfterEachEntry immediately flushes every log entry. Manual leaves flushing
// to FileSink::flush(), Logger::flushAllSinks(), or normal stream closure.
enum class FileFlushMode { AfterEachEntry, Manual };

// A vector keeps fields in the order supplied by the caller. Using strings for
// both parts keeps the C++11 API small and predictable.
using LogFields = std::vector<std::pair<std::string, std::string>>;

/**
 * @namespace cppcolorlogger_detail
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
namespace cppcolorlogger_detail {

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
  if (signature.find("<lambda") != std::string::npos || signature.find("(anonymous class)") != std::string::npos ||
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

} // namespace cppcolorlogger_detail

namespace cppcolorlogger_detail {

/**
 * @brief Checks whether a NO_COLOR environment value requests plain output.
 *
 * The NO_COLOR convention disables automatic color when the variable exists
 * and contains at least one character. An unset or empty value does not disable
 * color.
 */
inline bool hasNoColorValue(const char *value) {
  return value != nullptr && value[0] != '\0';
}

/**
 * @brief Applies the color-mode priority without accessing the operating system.
 *
 * Explicit enable or disable settings have highest priority. Automatic uses
 * color only when the output supports it and NO_COLOR is not requested.
 */
inline bool resolveColorEnabled(ColorMode mode, bool terminalSupportsColor, bool noColorRequested) {
  if (mode == ColorMode::Enabled)
    return true;
  if (mode == ColorMode::Disabled)
    return false;
  return terminalSupportsColor && !noColorRequested;
}

/**
 * @brief Detects whether the selected console stream can display ANSI colors.
 *
 * On Linux and macOS, isatty() checks whether the selected stream is connected
 * to a terminal instead of a file or pipe. An interactive terminal is assumed
 * to support ANSI colors.
 *
 * On Windows, GetStdHandle() finds the selected stream and GetConsoleMode()
 * verifies that it is a console. If needed, SetConsoleMode() attempts to enable
 * virtual-terminal processing, which allows ANSI color codes.
 *
 * Other platforms return false because color support cannot be confirmed. The
 * NO_COLOR environment variable is handled separately by
 * shouldUseConsoleColor().
 */
inline bool consoleSupportsColor(ConsoleStream stream) {
#if defined(_WIN32)
  const DWORD  handleId = stream == ConsoleStream::Stdout ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE;
  const HANDLE output   = GetStdHandle(handleId);
  if (output == INVALID_HANDLE_VALUE || output == nullptr)
    return false;

  DWORD mode = 0;
  if (GetConsoleMode(output, &mode) == 0)
    return false;

  const DWORD virtualTerminalProcessing = 0x0004;
  if ((mode & virtualTerminalProcessing) != 0)
    return true;
  return SetConsoleMode(output, mode | virtualTerminalProcessing) != 0;
#elif defined(__unix__) || defined(__APPLE__)
  const int fileDescriptor = stream == ConsoleStream::Stdout ? STDOUT_FILENO : STDERR_FILENO;
  return ::isatty(fileDescriptor) != 0;
#else
  (void)stream;
  return false;
#endif
}

/**
 * @brief Resolves the color mode against the current terminal and environment.
 *
 * Explicit enabling forces ANSI output. On Windows it also attempts to enable
 * virtual-terminal processing. Automatic mode respects both terminal support
 * and the NO_COLOR environment variable.
 */
inline bool shouldUseConsoleColor(ColorMode mode, ConsoleStream stream) {
  if (mode == ColorMode::Enabled) {
#if defined(_WIN32)
    (void)consoleSupportsColor(stream);
#endif
    return true;
  }
  if (mode == ColorMode::Disabled)
    return false;

  const bool noColorRequested      = hasNoColorValue(std::getenv("NO_COLOR"));
  const bool terminalSupportsColor = noColorRequested ? false : consoleSupportsColor(stream);
  return resolveColorEnabled(mode, terminalSupportsColor, noColorRequested);
}

} // namespace cppcolorlogger_detail

// A log event and its formatted representation. Destination-specific settings,
// such as console color, are provided to sinks separately through LogStyle.
struct LogEntry {
  LogLevel                              level;      // Message severity, such as Info, Warn, or Error.
  std::string                           message;    // Original message before metadata and fields are added.
  std::string                           context;    // Function or qualified method name; empty when not provided.
  LogFields                             fields;     // Ordered key/value data for structured sinks.
  std::chrono::system_clock::time_point eventTime;  // Raw creation time, available for precise custom formatting.
  std::string                           sourceFile; // Value captured from __FILE__; empty for an internal direct call.
  unsigned                              sourceLine; // Line captured from __LINE__; zero when no location was supplied.
  std::string                           threadId;   // Process-unique logger thread token formatted as text.
  std::string                           timestamp;  // Formatter-generated representation of eventTime.
  std::string                           formattedText; // Complete human-readable line for ordinary sinks.
};

// Presentation settings selected for one log call. Most sinks can ignore this;
// ConsoleSink uses it to decide whether to add ANSI color codes.
struct LogStyle {
  std::string color;     // ANSI color selected for this message's level.
  ColorMode   colorMode; // Determines whether ConsoleSink applies the color.
};

/**
 * Converts a LogEntry into the text written by ordinary sinks.
 *
 * Override format() to control the line layout. Override formatTimestamp() only
 * when the timestamp needs a different format. Return plain text because sinks
 * handle destination-specific behavior such as console color.
 */
class LogFormatter {
public:
  virtual ~LogFormatter() {}

  /** Creates the complete human-readable line stored in LogEntry::formattedText. */
  virtual std::string format(const LogEntry &entry) const = 0;

  /** Creates LogEntry::timestamp from its raw event time before format(). */
  virtual std::string formatTimestamp(const std::chrono::system_clock::time_point &eventTime) const {
    return formatTimeWithPattern(eventTime, "%Y-%m-%d %H:%M:%S");
  }

protected:
  /** Formats local time using the same placeholders accepted by std::strftime. */
  static std::string formatTimeWithPattern(const std::chrono::system_clock::time_point &eventTime,
                                           const std::string                           &pattern) {
    const std::time_t entryTime = std::chrono::system_clock::to_time_t(eventTime);
    std::tm           timeInfo  = {};
#if defined(_WIN32)
    if (localtime_s(&timeInfo, &entryTime) != 0)
      return std::string();
#else
    if (localtime_r(&entryTime, &timeInfo) == nullptr)
      return std::string();
#endif

    char timestamp[128] = {};
    if (std::strftime(timestamp, sizeof(timestamp), pattern.c_str(), &timeInfo) == 0)
      return std::string();
    return timestamp;
  }

  /** Formats UTC using the same placeholders accepted by std::strftime. */
  static std::string formatUtcTimeWithPattern(const std::chrono::system_clock::time_point &eventTime,
                                              const std::string                           &pattern) {
    const std::time_t entryTime = std::chrono::system_clock::to_time_t(eventTime);
    std::tm           timeInfo  = {};
#if defined(_WIN32)
    if (gmtime_s(&timeInfo, &entryTime) != 0)
      return std::string();
#else
    if (gmtime_r(&entryTime, &timeInfo) == nullptr)
      return std::string();
#endif

    char timestamp[128] = {};
    if (std::strftime(timestamp, sizeof(timestamp), pattern.c_str(), &timeInfo) == 0)
      return std::string();
    return timestamp;
  }

  /** Returns the millisecond component of an event time as a value from 0 to 999. */
  static unsigned millisecondsWithinSecond(const std::chrono::system_clock::time_point &eventTime) {
    const std::int64_t totalMilliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(eventTime.time_since_epoch()).count();
    std::int64_t milliseconds = totalMilliseconds % 1000;
    if (milliseconds < 0)
      milliseconds += 1000;
    return static_cast<unsigned>(milliseconds);
  }
};

/** Preserves the logger's standard human-readable output format. */
class DefaultLogFormatter : public LogFormatter {
public:
  std::string format(const LogEntry &entry) const override {
    std::ostringstream output;
    output << '[' << entry.timestamp << "] [" << logLevelToString(entry.level) << "] [" << entry.context << "] "
           << entry.message;

    if (!entry.fields.empty()) {
      output << " [";
      for (LogFields::const_iterator field = entry.fields.begin(); field != entry.fields.end(); ++field) {
        if (field != entry.fields.begin())
          output << ", ";
        output << field->first << '=' << field->second;
      }
      output << ']';
    }
    return output.str();
  }
};

/**
 * @brief Identifies one sink registration in one Logger.
 *
 * Keep the handle returned by Logger::addSink() when the sink may need to be
 * removed later. A handle belongs only to the Logger that created it. Copies of
 * the handle identify the same registration.
 */
class SinkHandle {
public:
  SinkHandle() : m_loggerToken(0), m_id(0) {}

  // True means the handle was created by a Logger. It does not guarantee that
  // the sink is still registered, because it may already have been removed.
  bool isValid() const {
    return m_loggerToken != 0;
  }

private:
  friend class Logger;

  SinkHandle(std::uint64_t loggerToken, std::size_t id) : m_loggerToken(loggerToken), m_id(id) {}

  std::uint64_t m_loggerToken;
  std::size_t   m_id;
};

class LogSink {
public:
  virtual ~LogSink() {}

  // Sinks without buffered output have nothing to flush and succeed by default.
  virtual bool flush() {
    return true;
  }

  /** Receives one log event and its destination-specific presentation settings. */
  virtual void write(const LogEntry &entry, const LogStyle &style) = 0;
};

class ConsoleSink : public LogSink {
public:
  explicit ConsoleSink(ConsoleStream stream = ConsoleStream::Stdout) : m_stream(stream) {}

  void write(const LogEntry &entry, const LogStyle &style) override {
    std::lock_guard<std::mutex> console_lck(m_console_mux);
    std::ostream               &output = outputStream();
    if (cppcolorlogger_detail::shouldUseConsoleColor(style.colorMode, m_stream) && !style.color.empty())
      output << style.color << entry.formattedText << Color::Reset << std::endl;
    else
      output << entry.formattedText << std::endl;
  }

  bool flush() override {
    std::lock_guard<std::mutex> console_lck(m_console_mux);
    std::ostream               &output = outputStream();
    output.flush();
    return static_cast<bool>(output);
  }

private:
  std::ostream &outputStream() {
    return m_stream == ConsoleStream::Stdout ? std::cout : std::cerr;
  }

  ConsoleStream m_stream;
  std::mutex    m_console_mux;
};

/**
 * Writes complete log entries to a file and stores the first I/O error.
 *
 * After an open, write, or flush failure, later writes are ignored. Check
 * hasError() and getLastError(), then replace the sink if recovery is needed.
 */
class FileSink : public LogSink {
public:
  explicit FileSink(const std::string &filename, FileOpenMode openMode = FileOpenMode::Append,
                    FileFlushMode flushMode = FileFlushMode::AfterEachEntry)
      : m_filename(filename), m_flushMode(flushMode) {
    const std::ios::openmode fileOpenMode =
        std::ios::out | (openMode == FileOpenMode::Append ? std::ios::app : std::ios::trunc);
    m_file.open(filename.c_str(), fileOpenMode);
    if (!m_file.is_open())
      m_lastError = "Failed to open log file '" + m_filename + "'.";
  }

  void write(const LogEntry &entry, const LogStyle &) override {
    std::lock_guard<std::mutex> file_lck(m_file_mux);
    if (!m_lastError.empty())
      return;

    m_file << entry.formattedText << '\n';
    if (m_flushMode == FileFlushMode::AfterEachEntry)
      m_file.flush();
    if (!m_file)
      m_lastError = "Failed to write to log file '" + m_filename + "'.";
  }

  /** Flushes buffered output and reports whether the operation succeeded. */
  bool flush() override {
    std::lock_guard<std::mutex> file_lck(m_file_mux);
    if (!m_lastError.empty())
      return false;

    m_file.flush();
    if (!m_file) {
      m_lastError = "Failed to flush log file '" + m_filename + "'.";
      return false;
    }
    return true;
  }

  bool isOpen() const {
    std::lock_guard<std::mutex> file_lck(m_file_mux);
    return m_file.is_open();
  }

  /** Returns true after an open, write, or flush operation has failed. */
  bool hasError() const {
    std::lock_guard<std::mutex> file_lck(m_file_mux);
    return !m_lastError.empty();
  }

  /** Returns a copy of the first error reported by this sink. */
  std::string getLastError() const {
    std::lock_guard<std::mutex> file_lck(m_file_mux);
    return m_lastError;
  }

private:
  mutable std::mutex m_file_mux;
  std::ofstream      m_file;
  std::string        m_filename;
  FileFlushMode      m_flushMode;
  std::string        m_lastError;
};

class InMemorySink : public LogSink {
public:
  void write(const LogEntry &entry, const LogStyle &) override {
    std::lock_guard<std::mutex> logs_lck(m_logs_mux);
    m_logs.push_back(entry.formattedText);
  }

  std::vector<std::string> getLogs() const {
    std::lock_guard<std::mutex> logs_lck(m_logs_mux);
    return m_logs;
  }

  void clear() {
    std::lock_guard<std::mutex> logs_lck(m_logs_mux);
    m_logs.clear();
  }

private:
  std::vector<std::string> m_logs;
  mutable std::mutex       m_logs_mux;
};

class Logger;
class ScopedSettings;

namespace cppcolorlogger_detail {
class LoggerTestAccess;
struct LoggerLogAccess;
Logger &defaultLogger();
} // namespace cppcolorlogger_detail

class Logger {
  typedef std::uint64_t ThreadToken;
  typedef std::uint64_t ScopeId;

  // A sink's level is part of its registration, so the same sink object can be
  // registered more than once with different limits and removal handles.
  struct RegisteredSink {
    RegisteredSink(const std::shared_ptr<LogSink> &registeredSink, LogLevel maximumLevel)
        : m_sink(registeredSink), m_maximumLevel(maximumLevel) {}

    // For example, an Info sink accepts Always through Info and rejects Debug
    // and Verbose.
    bool accepts(LogLevel messageLevel) const {
      return messageLevel <= m_maximumLevel;
    }

    std::shared_ptr<LogSink> m_sink;
    LogLevel                 m_maximumLevel;
  };

  typedef std::map<std::size_t, RegisteredSink> SinkMap;

  // All configurable values are kept together so a scoped override can copy
  // the calling thread's current configuration in one operation.
  struct Settings {
    Settings()
        : m_logLevel(LogLevel::Info),
          m_colors{{Color::White, Color::Magenta, Color::Red, Color::Yellow, Color::Green, Color::Cyan, Color::Blue}},
          m_colorMode(ColorMode::Automatic), m_formatter(std::make_shared<DefaultLogFormatter>()) {}

    LogLevel                      m_logLevel;
    std::set<LogLevel>            m_allowedLevels;
    std::array<std::string, 7>    m_colors;
    ColorMode                     m_colorMode;
    SinkMap                       m_sinks;
    std::shared_ptr<InMemorySink> m_inMemorySink;
    std::shared_ptr<LogFormatter> m_formatter;
  };

  struct ScopedSettingsEntry {
    ScopeId  m_id;
    Settings m_settings;
  };

  struct OutputSettings {
    OutputSettings() {
      m_style.color     = Color::White;
      m_style.colorMode = ColorMode::Automatic;
    }

    LogStyle                              m_style;
    std::vector<std::shared_ptr<LogSink>> m_sinks;
    std::shared_ptr<LogFormatter>         m_formatter;
  };

private:
  friend class ScopedSettings;
  friend class cppcolorlogger_detail::LoggerTestAccess;
  friend struct cppcolorlogger_detail::LoggerLogAccess;
  friend Logger &cppcolorlogger_detail::defaultLogger();

  // Applications use the shared LOGGER instance. LoggerTestAccess is declared
  // consistently above and defined only by the unit tests when isolated logger
  // state is required.
  explicit Logger(bool addDefaultConsoleSink = true)
      : m_nextSinkId(1), m_nextScopeId(1), m_loggerToken(createLoggerToken()) {
    if (addDefaultConsoleSink)
      m_defaultConsoleSinkHandle = addSinkLocked(m_globalSettings, std::make_shared<ConsoleSink>());
  }

public:
  Logger(const Logger &)            = delete;
  Logger &operator=(const Logger &) = delete;

  void setLogLevel(LogLevel level) {
    std::lock_guard<std::mutex> state_lck(m_state_mux);
    activeSettingsLocked(getCurrentThreadToken()).m_logLevel = level;
  }

  void setLevelColor(LogLevel level, const std::string &color) {
    const std::size_t           index = levelIndex(level);
    std::lock_guard<std::mutex> state_lck(m_state_mux);
    Settings                   &state = activeSettingsLocked(getCurrentThreadToken());
    if (index < state.m_colors.size())
      state.m_colors[index] = color;
  }

  // Select automatic, always-enabled, or always-disabled console color for the
  // calling thread's active settings.
  void setColorMode(ColorMode mode) {
    std::lock_guard<std::mutex> state_lck(m_state_mux);
    activeSettingsLocked(getCurrentThreadToken()).m_colorMode = mode;
  }

  /**
   * Uses a custom formatter for the calling thread's active settings.
   *
   * A null pointer is ignored, so logging always has a valid formatter.
   */
  void setFormatter(const std::shared_ptr<LogFormatter> &newFormatter) {
    if (!newFormatter)
      return;
    std::lock_guard<std::mutex> state_lck(m_state_mux);
    activeSettingsLocked(getCurrentThreadToken()).m_formatter = newFormatter;
  }

  /** Restores the standard timestamp and line layout. */
  void useDefaultFormatter() {
    setFormatter(std::make_shared<DefaultLogFormatter>());
  }

  /**
   * @brief Adds a sink and returns the handle needed to remove it later.
   *
   * A null sink is ignored and returns an invalid handle. Inside a settings
   * scope, the sink is added only to that temporary settings copy.
   */
  SinkHandle addSink(const std::shared_ptr<LogSink> &sink) {
    return addSink(sink, LogLevel::Verbose);
  }

  /** Adds a sink that receives only messages at or above its chosen severity. */
  SinkHandle addSink(const std::shared_ptr<LogSink> &sink, LogLevel maximumLevel) {
    if (!sink)
      return SinkHandle();
    std::lock_guard<std::mutex> state_lck(m_state_mux);
    return addSinkLocked(activeSettingsLocked(getCurrentThreadToken()), sink, maximumLevel);
  }

  /**
   * @brief Adds a console sink and returns its removal handle.
   *
   * Stdout is used by default. Select Stderr to keep diagnostic logs separate
   * from normal program output. Use this method to restore console logging after
   * removing the default console sink or calling clearSinks().
   */
  SinkHandle addConsoleSink(ConsoleStream stream = ConsoleStream::Stdout) {
    return addSink(std::make_shared<ConsoleSink>(stream));
  }

  /** Returns the handle created for this Logger's initial console sink. */
  SinkHandle getDefaultConsoleSinkHandle() const {
    return m_defaultConsoleSinkHandle;
  }

  /**
   * @brief Removes one sink from the calling thread's active settings.
   *
   * Returns false for an invalid, foreign, unknown, or already-removed handle.
   * A write that already copied the sink may finish after this method returns;
   * its shared ownership keeps that write safe.
   */
  bool removeSink(const SinkHandle &handle) {
    if (handle.m_loggerToken != m_loggerToken)
      return false;

    std::lock_guard<std::mutex> state_lck(m_state_mux);
    Settings                   &state = activeSettingsLocked(getCurrentThreadToken());
    SinkMap::iterator           sink  = state.m_sinks.find(handle.m_id);
    if (sink == state.m_sinks.end())
      return false;

    const bool removedMemorySink = state.m_inMemorySink && sink->second.m_sink.get() == state.m_inMemorySink.get();
    state.m_sinks.erase(sink);
    if (removedMemorySink && !containsSinkLocked(state, state.m_inMemorySink))
      state.m_inMemorySink.reset();
    return true;
  }

  /**
   * @brief Removes every sink from the calling thread's active settings.
   *
   * This includes the default console sink and the managed in-memory sink. Call
   * addConsoleSink() or enableInMemorySink() to add either one again.
   */
  void clearSinks() {
    std::lock_guard<std::mutex> state_lck(m_state_mux);
    Settings                   &state = activeSettingsLocked(getCurrentThreadToken());
    state.m_sinks.clear();
    state.m_inMemorySink.reset();
  }

  /** Adds a file sink and returns it for status checks and explicit flushing. */
  std::shared_ptr<FileSink> addFileSink(const std::string &filename, FileOpenMode openMode = FileOpenMode::Append,
                                        FileFlushMode flushMode = FileFlushMode::AfterEachEntry) {
    const std::shared_ptr<FileSink> sink = std::make_shared<FileSink>(filename, openMode, flushMode);
    addSink(sink);
    return sink;
  }

  std::shared_ptr<InMemorySink> enableInMemorySink() {
    std::lock_guard<std::mutex> state_lck(m_state_mux);
    Settings                   &state = activeSettingsLocked(getCurrentThreadToken());
    if (!state.m_inMemorySink) {
      state.m_inMemorySink = std::make_shared<InMemorySink>();
      addSinkLocked(state, state.m_inMemorySink);
    }
    return state.m_inMemorySink;
  }

  std::vector<std::string> getInMemoryLogs() const {
    std::shared_ptr<InMemorySink> sink;
    {
      std::lock_guard<std::mutex> state_lck(m_state_mux);
      sink = activeSettingsLocked(getCurrentThreadToken()).m_inMemorySink;
    }
    return sink ? sink->getLogs() : std::vector<std::string>();
  }

  /**
   * @brief Flushes every sink in the calling thread's active settings.
   *
   * The sink list is copied before flushing, so concurrent sink removal is safe.
   * A sink removed after the copy may receive this final flush. Returns true
   * only when every sink reports success.
   */
  bool flushAllSinks() {
    std::vector<std::shared_ptr<LogSink>> sinks;
    {
      std::lock_guard<std::mutex> state_lck(m_state_mux);
      const Settings             &state = activeSettingsLocked(getCurrentThreadToken());
      // Keep shared ownership while flushing without the settings lock.
      sinks.reserve(state.m_sinks.size());
      for (SinkMap::const_iterator sink = state.m_sinks.begin(); sink != state.m_sinks.end(); ++sink)
        sinks.push_back(sink->second.m_sink);
    }

    bool                                  succeeded = true;
    std::lock_guard<std::recursive_mutex> output_lck(m_output_mux);
    for (std::vector<std::shared_ptr<LogSink>>::iterator sink = sinks.begin(); sink != sinks.end(); ++sink) {
      try {
        if (!(*sink)->flush())
          succeeded = false;
      } catch (const std::exception &error) {
        recordError(std::string("A sink failed while flushing: ") + error.what());
        succeeded = false;
      } catch (...) {
        recordError("A sink failed while flushing with an unknown error.");
        succeeded = false;
      }
    }
    return succeeded;
  }

  /** Returns true when a formatter or sink has reported an error. */
  bool hasError() const {
    std::lock_guard<std::mutex> error_lck(m_error_mux);
    return !m_lastError.empty();
  }

  /** Returns the most recent formatter, sink, or recursion error. */
  std::string getLastError() const {
    std::lock_guard<std::mutex> error_lck(m_error_mux);
    return m_lastError;
  }

  /** Clears the logger-level error status. FileSink has its own I/O status. */
  void clearError() {
    std::lock_guard<std::mutex> error_lck(m_error_mux);
    m_lastError.clear();
  }

  void setAllowedLevels(std::initializer_list<LogLevel> levels) {
    std::lock_guard<std::mutex> state_lck(m_state_mux);
    activeSettingsLocked(getCurrentThreadToken()).m_allowedLevels = std::set<LogLevel>(levels.begin(), levels.end());
  }

  void clearAllowedLevels() {
    std::lock_guard<std::mutex> state_lck(m_state_mux);
    activeSettingsLocked(getCurrentThreadToken()).m_allowedLevels.clear();
  }

  ScopedSettings scopedSettings();

  /**
   * @brief Checks whether a log level is currently enabled.
   *
   * Uses the calling thread's active threshold, allowed-level list, and sink
   * limits. This only checks the current settings; it does not create or write
   * a message. The returned value is a snapshot and can become outdated if the
   * settings change afterward.
   *
   * @param level The level that a future message would use.
   * @return True when the level passes the logger filters and at least one sink accepts it.
   */
  bool isEnabled(LogLevel level) const {
    std::lock_guard<std::mutex> state_lck(m_state_mux);
    const Settings             &state = activeSettingsLocked(getCurrentThreadToken());
    return acceptsLevel(state, level) && anySinkAcceptsLevelLocked(state, level);
  }

private:
  template <typename T>
  void log(LogLevel level, const T &message, const std::string &context = "", const char *sourceFile = nullptr,
           unsigned sourceLine = 0) {
    logImpl(level, message, LogFields(), context, sourceFile, sourceLine);
  }

  /** Logs a message with ordered key/value fields for structured sinks. */
  template <typename T>
  void log(LogLevel level, const T &message, const LogFields &fields, const std::string &context = "",
           const char *sourceFile = nullptr, unsigned sourceLine = 0) {
    logImpl(level, message, fields, context, sourceFile, sourceLine);
  }

  template <typename T>
  void logImpl(LogLevel level, const T &message, const LogFields &fields, const std::string &context,
               const char *sourceFile, unsigned sourceLine) {
    // Avoid converting a disabled value to text. Expressions passed as
    // `message` have already been evaluated by the caller; use isEnabled()
    // before the call when creating the value itself is expensive.
    OutputSettings outputSettings;
    if (!tryCaptureOutputSettings(level, outputSettings))
      return;

    unsigned &loggingDepth = currentLoggingDepth();
    if (loggingDepth >= maximumLoggingDepth()) {
      recordError("Logging stopped because a formatter or sink repeatedly called the logger.");
      return;
    }
    LoggingDepthGuard depthGuard(loggingDepth);

    try {
      LogEntry entry   = {};
      entry.level      = level;
      entry.context    = context;
      entry.fields     = fields;
      entry.eventTime  = std::chrono::system_clock::now();
      entry.sourceFile = sourceFile == nullptr ? std::string() : sourceFile;
      entry.sourceLine = sourceLine;
      entry.threadId   = threadTokenToString(getCurrentThreadToken());

      std::ostringstream stream;
      stream << message;
      entry.message = stream.str();
      formatAndWriteToSinks(entry, outputSettings);
    } catch (const std::exception &error) {
      recordError(std::string("The log message could not be created: ") + error.what());
    } catch (...) {
      recordError("The log message could not be created because of an unknown error.");
    }
  }

  class LoggingDepthGuard {
  public:
    explicit LoggingDepthGuard(unsigned &depth) : m_depth(depth) {
      ++m_depth;
    }

    ~LoggingDepthGuard() {
      --m_depth;
    }

  private:
    unsigned &m_depth;
  };

  static unsigned maximumLoggingDepth() {
    return 8;
  }

  static unsigned &currentLoggingDepth() {
    static thread_local unsigned depth = 0;
    return depth;
  }

  SinkHandle addSinkLocked(Settings &state, const std::shared_ptr<LogSink> &sink,
                           LogLevel maximumLevel = LogLevel::Verbose) {
    const std::size_t id = m_nextSinkId++;
    if (id == 0)
      std::abort();
    state.m_sinks.insert(std::make_pair(id, RegisteredSink(sink, maximumLevel)));
    return SinkHandle(m_loggerToken, id);
  }

  static bool containsSinkLocked(const Settings &state, const std::shared_ptr<LogSink> &wanted) {
    for (SinkMap::const_iterator sink = state.m_sinks.begin(); sink != state.m_sinks.end(); ++sink) {
      if (sink->second.m_sink.get() == wanted.get())
        return true;
    }
    return false;
  }

  /**
   * Returns true when at least one registered sink accepts `messageLevel`.
   *
   * `isEnabled()` uses this check to answer whether a message could reach any
   * sink. It only checks registrations and does not copy sinks or write a
   * message.
   *
   * The `Locked` suffix means the caller must already hold `m_state_mux`.
   */
  static bool anySinkAcceptsLevelLocked(const Settings &state, LogLevel messageLevel) {
    for (SinkMap::const_iterator sink = state.m_sinks.begin(); sink != state.m_sinks.end(); ++sink) {
      if (sink->second.accepts(messageLevel))
        return true;
    }
    return false;
  }

  /**
   * Copies only the registered sinks that accept `messageLevel` into `sinks`.
   *
   * A log call uses this helper after the message passes the logger's global
   * threshold and allowed-level list. For example, a Debug message is copied to
   * a Debug sink but not to an Info sink. Copying `shared_ptr`s lets the selected
   * sinks remain alive while output occurs without holding the settings mutex.
   *
   * The `Locked` suffix means the caller must already hold `m_state_mux`.
   */
  static void copySinksAcceptingLevelLocked(const Settings &state, LogLevel messageLevel,
                                            std::vector<std::shared_ptr<LogSink>> &sinks) {
    sinks.reserve(state.m_sinks.size());
    for (SinkMap::const_iterator sink = state.m_sinks.begin(); sink != state.m_sinks.end(); ++sink) {
      if (sink->second.accepts(messageLevel))
        sinks.push_back(sink->second.m_sink);
    }
  }

  static std::size_t levelIndex(LogLevel level) {
    return static_cast<std::size_t>(level);
  }

  static std::string threadTokenToString(ThreadToken threadToken) {
    std::ostringstream output;
    output << threadToken;
    return output.str();
  }

  // Assign each thread a process-wide token the first time it uses a logger.
  // Unlike std::thread::id, this value is never recycled after a thread exits.
  static ThreadToken getCurrentThreadToken() {
    static std::atomic<ThreadToken>       nextThreadToken(1);
    static thread_local const ThreadToken threadToken = nextThreadToken.fetch_add(1, std::memory_order_relaxed);
    if (threadToken == 0)
      std::abort();
    return threadToken;
  }

  static std::uint64_t createLoggerToken() {
    static std::atomic<std::uint64_t> nextLoggerToken(1);
    const std::uint64_t               loggerToken = nextLoggerToken.fetch_add(1, std::memory_order_relaxed);
    if (loggerToken == 0)
      std::abort();
    return loggerToken;
  }

  // The caller must hold m_state_mux. Setters and logging use the current
  // thread's top override when present, otherwise they use the global state.
  Settings &activeSettingsLocked(ThreadToken threadToken) {
    std::map<ThreadToken, std::vector<ScopedSettingsEntry>>::iterator stack = m_scopedSettingsStacks.find(threadToken);
    return stack == m_scopedSettingsStacks.end() || stack->second.empty() ? m_globalSettings
                                                                          : stack->second.back().m_settings;
  }

  const Settings &activeSettingsLocked(ThreadToken threadToken) const {
    std::map<ThreadToken, std::vector<ScopedSettingsEntry>>::const_iterator stack =
        m_scopedSettingsStacks.find(threadToken);
    return stack == m_scopedSettingsStacks.end() || stack->second.empty() ? m_globalSettings
                                                                          : stack->second.back().m_settings;
  }

  // Keep the threshold and allowed-level rule in one place for isEnabled() and log().
  static bool acceptsLevel(const Settings &state, LogLevel level) {
    return level <= state.m_logLevel && (state.m_allowedLevels.empty() || state.m_allowedLevels.count(level) != 0);
  }

  ScopeId beginSettingsOverride(ThreadToken threadToken) {
    std::lock_guard<std::mutex>       state_lck(m_state_mux);
    std::vector<ScopedSettingsEntry> &stack   = m_scopedSettingsStacks[threadToken];
    const ScopeId                     scopeId = m_nextScopeId++;
    if (scopeId == 0)
      std::abort();
    stack.push_back(ScopedSettingsEntry{scopeId, stack.empty() ? m_globalSettings : stack.back().m_settings});
    return scopeId;
  }

  void endSettingsOverride(ThreadToken threadToken, ScopeId scopeId) {
    std::lock_guard<std::mutex>                                       state_lck(m_state_mux);
    std::map<ThreadToken, std::vector<ScopedSettingsEntry>>::iterator stack = m_scopedSettingsStacks.find(threadToken);
    if (stack == m_scopedSettingsStacks.end() || stack->second.empty())
      return;

    for (std::vector<ScopedSettingsEntry>::iterator state = stack->second.begin(); state != stack->second.end();
         ++state) {
      if (state->m_id == scopeId) {
        stack->second.erase(state);
        break;
      }
    }
    if (stack->second.empty())
      m_scopedSettingsStacks.erase(stack);
  }

  // Check the level and copy the selected color, formatter, and sinks while
  // holding the state lock. Formatting and output happen after releasing it.
  bool tryCaptureOutputSettings(LogLevel level, OutputSettings &outputSettings) const {
    std::lock_guard<std::mutex> state_lck(m_state_mux);
    const Settings             &state = activeSettingsLocked(getCurrentThreadToken());
    if (!acceptsLevel(state, level))
      return false;

    copySinksAcceptingLevelLocked(state, level, outputSettings.m_sinks);
    if (outputSettings.m_sinks.empty())
      return false;

    const std::size_t index          = levelIndex(level);
    outputSettings.m_style.color     = index < state.m_colors.size() ? state.m_colors[index] : Color::White;
    outputSettings.m_style.colorMode = state.m_colorMode;
    outputSettings.m_formatter       = state.m_formatter;
    return true;
  }

  void formatAndWriteToSinks(LogEntry &entry, const OutputSettings &outputSettings) {
    // Formatting and sink writes are serialized. This lets a formatter safely
    // keep internal state when it is used by this Logger.
    std::lock_guard<std::recursive_mutex> output_lck(m_output_mux);
    try {
      entry.timestamp     = outputSettings.m_formatter->formatTimestamp(entry.eventTime);
      entry.formattedText = outputSettings.m_formatter->format(entry);
    } catch (const std::exception &error) {
      recordError(std::string("The log formatter failed: ") + error.what());
      return;
    } catch (...) {
      recordError("The log formatter failed with an unknown error.");
      return;
    }

    for (std::vector<std::shared_ptr<LogSink>>::const_iterator sink = outputSettings.m_sinks.begin();
         sink != outputSettings.m_sinks.end(); ++sink) {
      try {
        (*sink)->write(entry, outputSettings.m_style);
      } catch (const std::exception &error) {
        recordError(std::string("A log sink failed: ") + error.what());
      } catch (...) {
        recordError("A log sink failed with an unknown error.");
      }
    }
  }

  void recordError(const std::string &message) {
    std::lock_guard<std::mutex> error_lck(m_error_mux);
    m_lastError = message;
  }

  mutable std::mutex   m_state_mux;
  mutable std::mutex   m_error_mux;
  std::recursive_mutex m_output_mux;
  Settings             m_globalSettings;
  std::size_t          m_nextSinkId;
  ScopeId              m_nextScopeId;
  const std::uint64_t  m_loggerToken;
  SinkHandle           m_defaultConsoleSinkHandle;
  std::string          m_lastError;

  // Unique tokens keep stacks separate even when the operating system reuses a
  // thread ID. A moved guard keeps its creating thread's token for cleanup.
  std::map<ThreadToken, std::vector<ScopedSettingsEntry>> m_scopedSettingsStacks;
};

namespace cppcolorlogger_detail {

// Logging macros use this friend to reach Logger::log() without making direct
// logging part of the public Logger API.
struct LoggerLogAccess {
  template <typename T>
  static void log(Logger &logger, LogLevel level, const T &message, const std::string &context, const char *sourceFile,
                  unsigned sourceLine) {
    logger.log(level, message, context, sourceFile, sourceLine);
  }

  template <typename T>
  static void log(Logger &logger, LogLevel level, const T &message, const LogFields &fields, const std::string &context,
                  const char *sourceFile, unsigned sourceLine) {
    logger.log(level, message, fields, context, sourceFile, sourceLine);
  }
};

} // namespace cppcolorlogger_detail

class ScopedSettings {
public:
  explicit ScopedSettings(Logger &logger)
      : m_logger(&logger), m_ownerThreadToken(Logger::getCurrentThreadToken()),
        m_scopeId(m_logger->beginSettingsOverride(m_ownerThreadToken)) {}

  ~ScopedSettings() {
    if (m_logger)
      m_logger->endSettingsOverride(m_ownerThreadToken, m_scopeId);
  }

  ScopedSettings(ScopedSettings &&other) noexcept
      : m_logger(other.m_logger), m_ownerThreadToken(other.m_ownerThreadToken), m_scopeId(other.m_scopeId) {
    other.m_logger = nullptr;
  }

  ScopedSettings(const ScopedSettings &)            = delete;
  ScopedSettings &operator=(const ScopedSettings &) = delete;
  ScopedSettings &operator=(ScopedSettings &&)      = delete;

private:
  Logger *m_logger;
  // Keep the creating thread's unique token so a moved guard restores the
  // correct stack even after that thread exits.
  Logger::ThreadToken m_ownerThreadToken;
  Logger::ScopeId     m_scopeId;
};

inline ScopedSettings Logger::scopedSettings() {
  return ScopedSettings(*this);
}

namespace cppcolorlogger_detail {

inline Logger &defaultLogger() {
  static Logger logger;
  return logger;
}

} // namespace cppcolorlogger_detail

/** The shared logger instance used by the convenience macros. */
#define LOGGER (::cppcolorlogger_detail::defaultLogger())

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
 * Example: LOGGER_LOG(LogLevel::Info, "Server started");
 */
#define LOGGER_LOG(level, message)                                                                                     \
  ::cppcolorlogger_detail::LoggerLogAccess::log(                                                                       \
      LOGGER, level, message, ::cppcolorlogger_detail::normalizeFunctionSignature(CPPCOLORLOG_SIGNATURE, __func__),    \
      __FILE__, __LINE__)

/**
 * Logs a message with key/value fields and automatic source context.
 * The variadic parameter allows a braced field list to contain commas.
 * Example: LOGGER_LOG_FIELDS(LogLevel::Info, "Done", {{"status", "200"}});
 */
#define LOGGER_LOG_FIELDS(level, message, ...)                                                                         \
  ::cppcolorlogger_detail::LoggerLogAccess::log(                                                                       \
      LOGGER, level, message, __VA_ARGS__,                                                                             \
      ::cppcolorlogger_detail::normalizeFunctionSignature(CPPCOLORLOG_SIGNATURE, __func__), __FILE__, __LINE__)

/**
 * Logs with a name chosen by the caller. Prefer this for meaningful lambda
 * names or context text that must be identical on every compiler.
 * Example: LOGGER_LOG_WITH_CONTEXT(LogLevel::Info, "worker", "Task started");
 */
#define LOGGER_LOG_WITH_CONTEXT(level, context, message)                                                               \
  ::cppcolorlogger_detail::LoggerLogAccess::log(LOGGER, level, message, context, __FILE__, __LINE__)

#endif // CPPCOLORLOGGER_LOGGER_H
