#ifndef CPPCOLORLOGGER_LOGGER_H
#define CPPCOLORLOGGER_LOGGER_H

#include <atomic>
#include <array>
#include <cctype>
#include <cstdint>
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
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#ifdef ERROR
#undef ERROR
#endif
#elif defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
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

enum class LOGLEVEL { ALWAYS, FATAL, ERROR, WARN, INFO, DEBUG, VERBOSE };

inline const char *toString(LOGLEVEL level) {
  switch (level) {
  case LOGLEVEL::ALWAYS:
    return "ALWAYS";
  case LOGLEVEL::FATAL:
    return "FATAL";
  case LOGLEVEL::ERROR:
    return "ERROR";
  case LOGLEVEL::WARN:
    return "WARN";
  case LOGLEVEL::INFO:
    return "INFO";
  case LOGLEVEL::DEBUG:
    return "DEBUG";
  case LOGLEVEL::VERBOSE:
    return "VERBOSE";
  }
  return "UNKNOWN";
}

namespace cppcolorlog {

// AUTOMATIC is the safe default: use color for a supported interactive terminal,
// but not for redirected output or when NO_COLOR is set. ENABLED always emits
// ANSI colors, and DISABLED always emits plain text.
enum class ColorMode { AUTOMATIC, ENABLED, DISABLED };

// APPEND preserves existing file contents. TRUNCATE clears the file when the
// sink opens it.
enum class FileOpenMode { APPEND, TRUNCATE };

// A vector keeps fields in the order supplied by the caller. Using strings for
// both parts keeps the C++11 API small and predictable.
using LogFields = std::vector<std::pair<std::string, std::string>>;

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

namespace detail {

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
 * Explicit enable or disable settings have highest priority. AUTOMATIC uses
 * color only when the output supports it and NO_COLOR is not requested.
 */
inline bool resolveColorEnabled(ColorMode mode, bool terminalSupportsColor, bool noColorRequested) {
  if (mode == ColorMode::ENABLED)
    return true;
  if (mode == ColorMode::DISABLED)
    return false;
  return terminalSupportsColor && !noColorRequested;
}

/**
 * @brief Detects whether standard output can display ANSI colors.
 *
 * On Linux and macOS, isatty(STDOUT_FILENO) checks whether stdout is connected
 * to a terminal instead of a file or pipe. An interactive terminal is assumed
 * to support ANSI colors.
 *
 * On Windows, GetStdHandle() finds stdout and GetConsoleMode() verifies that it
 * is a console. If needed, SetConsoleMode() attempts to enable virtual-terminal
 * processing, which allows the console to understand ANSI color codes.
 *
 * Other platforms return false because color support cannot be confirmed. The
 * NO_COLOR environment variable is handled separately by
 * shouldUseConsoleColor().
 */
inline bool consoleSupportsColor() {
#if defined(_WIN32)
  const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
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
  return ::isatty(STDOUT_FILENO) != 0;
#else
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
inline bool shouldUseConsoleColor(ColorMode mode) {
  if (mode == ColorMode::ENABLED) {
#if defined(_WIN32)
    (void)consoleSupportsColor();
#endif
    return true;
  }
  if (mode == ColorMode::DISABLED)
    return false;

  const bool noColorRequested      = hasNoColorValue(std::getenv("NO_COLOR"));
  const bool terminalSupportsColor = noColorRequested ? false : consoleSupportsColor();
  return resolveColorEnabled(mode, terminalSupportsColor, noColorRequested);
}

} // namespace detail

// Existing sinks can continue using text. Structured sinks can use the separate
// values below without parsing that human-readable text.
struct LogEntry {
  LOGLEVEL    level;     // Message severity, such as INFO, WARN, or ERROR.
  std::string text;      // Complete human-readable line that a basic sink can write directly.
  std::string color;     // ANSI color selected for this level; used by color-capable sinks.
  ColorMode   colorMode; // Determines whether console color is automatic, enabled, or disabled.
  std::string timestamp; // Time when the entry was created, without surrounding brackets.
  std::string message;   // Original message before the logger adds metadata and fields.
  std::string function;  // Function that created the entry; empty when no context was provided.
  std::string className; // Class containing the function; empty for a standalone function.
  LogFields   fields;    // Structured key/value data that sinks can inspect without parsing text.
};

namespace detail {

/** Combines separate class and function names for display and serialization. */
inline std::string contextName(const std::string &function, const std::string &className) {
  return className.empty() ? function : className + "::" + function;
}

/** Escapes one string so it can be safely placed between JSON quotes. */
inline std::string escapeJsonString(const std::string &value) {
  static const char hexadecimal[] = "0123456789abcdef";
  std::string       escaped;

  for (std::string::const_iterator character = value.begin(); character != value.end(); ++character) {
    const unsigned char byte = static_cast<unsigned char>(*character);
    switch (byte) {
    case '"':
      escaped += "\\\"";
      break;
    case '\\':
      escaped += "\\\\";
      break;
    case '\b':
      escaped += "\\b";
      break;
    case '\f':
      escaped += "\\f";
      break;
    case '\n':
      escaped += "\\n";
      break;
    case '\r':
      escaped += "\\r";
      break;
    case '\t':
      escaped += "\\t";
      break;
    default:
      if (byte < 0x20) {
        const char unicodeEscape[] = {'\\', 'u', '0', '0', hexadecimal[byte >> 4], hexadecimal[byte & 0x0f]};
        escaped.append(unicodeEscape, sizeof(unicodeEscape));
      } else {
        escaped += static_cast<char>(byte);
      }
    }
  }
  return escaped;
}

/**
 * Serializes an entry for testing and future JSON sinks.
 *
 * This helper only creates JSON text; it does not write to a file. Fields are
 * emitted in their original order.
 */
inline std::string serializeLogEntryToJson(const LogEntry &entry) {
  std::ostringstream json;
  json << "{\"timestamp\":\"" << escapeJsonString(entry.timestamp) << "\",\"level\":\"" << toString(entry.level)
       << "\",\"context\":\"" << escapeJsonString(contextName(entry.function, entry.className)) << "\",\"message\":\""
       << escapeJsonString(entry.message) << "\",\"fields\":{";

  for (LogFields::const_iterator field = entry.fields.begin(); field != entry.fields.end(); ++field) {
    if (field != entry.fields.begin())
      json << ',';
    json << '"' << escapeJsonString(field->first) << "\":\"" << escapeJsonString(field->second) << '"';
  }
  json << "}}";
  return json.str();
}

} // namespace detail

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

  /** Creates the complete human-readable line stored in LogEntry::text. */
  virtual std::string format(const LogEntry &entry) const = 0;

  /** Creates LogEntry::timestamp before format() receives the entry. */
  virtual std::string formatTimestamp(std::time_t entryTime) const {
    return formatTimeWithPattern(entryTime, "%Y-%m-%d %H:%M:%S");
  }

protected:
  /** Formats a time using the same placeholders accepted by std::strftime. */
  static std::string formatTimeWithPattern(std::time_t entryTime, const std::string &pattern) {
    std::tm timeInfo = {};
#if defined(_WIN32)
    localtime_s(&timeInfo, &entryTime);
#else
    localtime_r(&entryTime, &timeInfo);
#endif

    char timestamp[128] = {};
    if (std::strftime(timestamp, sizeof(timestamp), pattern.c_str(), &timeInfo) == 0)
      return std::string();
    return timestamp;
  }
};

/** Preserves the logger's standard human-readable output format. */
class DefaultLogFormatter : public LogFormatter {
public:
  std::string format(const LogEntry &entry) const override {
    std::ostringstream output;
    output << '[' << entry.timestamp << "] [" << toString(entry.level) << "] ["
           << detail::contextName(entry.function, entry.className) << "] " << entry.message;

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

class Logger;

/**
 * @brief Identifies one sink registration in one Logger.
 *
 * Keep the handle returned by Logger::addSink() when the sink may need to be
 * removed later. A handle belongs only to the Logger that created it. Copies of
 * the handle identify the same registration.
 */
class SinkHandle {
public:
  SinkHandle() : m_logger(nullptr), m_id(0) {}

  // True means the handle was created by a Logger. It does not guarantee that
  // the sink is still registered, because it may already have been removed.
  bool isValid() const {
    return m_logger != nullptr;
  }

private:
  friend class Logger;

  SinkHandle(const Logger *logger, std::size_t id) : m_logger(logger), m_id(id) {}

  const Logger *m_logger;
  std::size_t   m_id;
};

class LogSink {
public:
  virtual ~LogSink() {}

  virtual bool supportsColor() const {
    return false;
  }

  // Sinks without buffered output have nothing to flush and succeed by default.
  virtual bool flush() {
    return true;
  }

  /** Receives the formatted text and the original structured log data. */
  virtual void write(const LogEntry &entry) = 0;
};

class ConsoleSink : public LogSink {
public:
  bool supportsColor() const override {
    return true;
  }

  void write(const LogEntry &entry) override {
    std::lock_guard<std::mutex> console_lck(m_console_mux);
    if (detail::shouldUseConsoleColor(entry.colorMode) && !entry.color.empty())
      std::cout << entry.color << entry.text << Color::RESET << std::endl;
    else
      std::cout << entry.text << std::endl;
  }

  bool flush() override {
    std::lock_guard<std::mutex> console_lck(m_console_mux);
    std::cout.flush();
    return static_cast<bool>(std::cout);
  }

private:
  std::mutex m_console_mux;
};

/**
 * Writes complete log entries to a file and stores the first I/O error.
 *
 * After an open, write, or flush failure, later writes are ignored. Check
 * hasError() and getLastError(), then replace the sink if recovery is needed.
 */
class FileSink : public LogSink {
public:
  explicit FileSink(const std::string &filename, FileOpenMode mode = FileOpenMode::APPEND) : m_filename(filename) {
    const std::ios::openmode openMode =
        std::ios::out | (mode == FileOpenMode::APPEND ? std::ios::app : std::ios::trunc);
    m_file.open(filename.c_str(), openMode);
    if (!m_file.is_open())
      m_lastError = "Failed to open log file '" + m_filename + "'.";
  }

  void write(const LogEntry &entry) override {
    std::lock_guard<std::mutex> file_lck(m_file_mux);
    if (!m_lastError.empty())
      return;

    m_file << entry.text << std::endl;
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
  std::string        m_lastError;
};

class InMemorySink : public LogSink {
public:
  void write(const LogEntry &entry) override {
    std::lock_guard<std::mutex> logs_lck(m_logs_mux);
    m_logs.push_back(entry.text);
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

struct LoggerState {
  LoggerState()
      : logLevel(LOGLEVEL::INFO),
        colors{{Color::WHITE, Color::MAGENTA, Color::RED, Color::YELLOW, Color::GREEN, Color::CYAN, Color::BLUE}},
        colorMode(ColorMode::AUTOMATIC), formatter(std::make_shared<DefaultLogFormatter>()) {}

  LOGLEVEL                                        logLevel;
  std::set<LOGLEVEL>                              filterLevels;
  std::array<std::string, 7>                      colors;
  ColorMode                                       colorMode;
  std::map<std::size_t, std::shared_ptr<LogSink>> sinks;
  std::shared_ptr<InMemorySink>                   inMemorySink;
  std::shared_ptr<LogFormatter>                   formatter;
};

class ScopedSettings;

class Logger {
  typedef std::uint64_t ThreadToken;

  struct OutputSettings {
    OutputSettings() : color(Color::WHITE), colorMode(ColorMode::AUTOMATIC) {}

    std::string                           color;
    ColorMode                             colorMode;
    std::vector<std::shared_ptr<LogSink>> sinks;
    std::shared_ptr<LogFormatter>         formatter;
  };

public:
  // Public construction enables isolated logger instances in tests and tools.
  explicit Logger(bool addDefaultConsoleSink = true) : m_nextSinkId(1) {
    if (addDefaultConsoleSink)
      m_defaultConsoleSinkHandle = addSinkLocked(m_globalState, std::make_shared<ConsoleSink>());
  }

  static Logger &getInstance() {
    static Logger instance;
    return instance;
  }

  Logger(const Logger &)            = delete;
  Logger &operator=(const Logger &) = delete;

  void setLogLevel(LOGLEVEL level) {
    std::lock_guard<std::mutex> state_lck(m_state_mux);
    activeStateLocked(getCurrentThreadToken()).logLevel = level;
  }

  void setLevelColor(LOGLEVEL level, const std::string &color) {
    const std::size_t           index = levelIndex(level);
    std::lock_guard<std::mutex> state_lck(m_state_mux);
    LoggerState                &state = activeStateLocked(getCurrentThreadToken());
    if (index < state.colors.size())
      state.colors[index] = color;
  }

  // Explicitly enable or disable ANSI color for the calling thread's active
  // settings. This choice overrides terminal detection and NO_COLOR.
  void setColorEnabled(bool enabled) {
    std::lock_guard<std::mutex> state_lck(m_state_mux);
    activeStateLocked(getCurrentThreadToken()).colorMode = enabled ? ColorMode::ENABLED : ColorMode::DISABLED;
  }

  // Return to automatic terminal detection and NO_COLOR handling.
  void useAutomaticColor() {
    std::lock_guard<std::mutex> state_lck(m_state_mux);
    activeStateLocked(getCurrentThreadToken()).colorMode = ColorMode::AUTOMATIC;
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
    activeStateLocked(getCurrentThreadToken()).formatter = newFormatter;
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
    if (!sink)
      return SinkHandle();
    std::lock_guard<std::mutex> state_lck(m_state_mux);
    return addSinkLocked(activeStateLocked(getCurrentThreadToken()), sink);
  }

  /**
   * @brief Adds a console sink and returns its removal handle.
   *
   * Use this to restore console logging after removing the default console sink
   * or calling clearSinks().
   */
  SinkHandle addConsoleSink() {
    return addSink(std::make_shared<ConsoleSink>());
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
    if (handle.m_logger != this)
      return false;

    std::lock_guard<std::mutex>                               state_lck(m_state_mux);
    LoggerState                                              &state = activeStateLocked(getCurrentThreadToken());
    std::map<std::size_t, std::shared_ptr<LogSink>>::iterator sink  = state.sinks.find(handle.m_id);
    if (sink == state.sinks.end())
      return false;

    const bool removedMemorySink = state.inMemorySink && sink->second.get() == state.inMemorySink.get();
    state.sinks.erase(sink);
    if (removedMemorySink && !containsSinkLocked(state, state.inMemorySink))
      state.inMemorySink.reset();
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
    LoggerState                &state = activeStateLocked(getCurrentThreadToken());
    state.sinks.clear();
    state.inMemorySink.reset();
  }

  /** Adds a file sink in append mode by default and returns it for status checks. */
  std::shared_ptr<FileSink> addFileSink(const std::string &filename, FileOpenMode mode = FileOpenMode::APPEND) {
    const std::shared_ptr<FileSink> sink = std::make_shared<FileSink>(filename, mode);
    addSink(sink);
    return sink;
  }

  std::shared_ptr<InMemorySink> enableInMemorySink() {
    std::lock_guard<std::mutex> state_lck(m_state_mux);
    LoggerState                &state = activeStateLocked(getCurrentThreadToken());
    if (!state.inMemorySink) {
      state.inMemorySink = std::make_shared<InMemorySink>();
      addSinkLocked(state, state.inMemorySink);
    }
    return state.inMemorySink;
  }

  std::vector<std::string> getInMemoryLogs() const {
    std::shared_ptr<InMemorySink> sink;
    {
      std::lock_guard<std::mutex> state_lck(m_state_mux);
      sink = activeStateLocked(getCurrentThreadToken()).inMemorySink;
    }
    return sink ? sink->getLogs() : std::vector<std::string>();
  }

  /**
   * @brief Flushes every active sink and reports whether all flushes succeeded.
   *
   * The sink list is copied before flushing, so concurrent sink removal is safe.
   * A sink removed after the copy may receive this final flush.
   */
  bool flush() {
    std::vector<std::shared_ptr<LogSink>> sinks;
    {
      std::lock_guard<std::mutex> state_lck(m_state_mux);
      copySinksLocked(activeStateLocked(getCurrentThreadToken()), sinks);
    }

    bool                                  succeeded = true;
    std::lock_guard<std::recursive_mutex> output_lck(m_output_mux);
    for (std::vector<std::shared_ptr<LogSink>>::iterator sink = sinks.begin(); sink != sinks.end(); ++sink) {
      if (!(*sink)->flush())
        succeeded = false;
    }
    return succeeded;
  }

  void setFilterLevels(std::initializer_list<LOGLEVEL> levels) {
    std::lock_guard<std::mutex> state_lck(m_state_mux);
    activeStateLocked(getCurrentThreadToken()).filterLevels = std::set<LOGLEVEL>(levels.begin(), levels.end());
  }

  void clearFilterLevels() {
    std::lock_guard<std::mutex> state_lck(m_state_mux);
    activeStateLocked(getCurrentThreadToken()).filterLevels.clear();
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
  bool isEnabled(LOGLEVEL level) const {
    std::lock_guard<std::mutex> state_lck(m_state_mux);
    return acceptsLevel(activeStateLocked(getCurrentThreadToken()), level);
  }

  template <typename T>
  void log(LOGLEVEL level, const T &message, const std::string &function = "", const std::string &className = "") {
    logImpl(level, message, LogFields(), function, className);
  }

  /** Logs a message with ordered key/value fields for structured sinks. */
  template <typename T>
  void log(LOGLEVEL level, const T &message, const LogFields &fields, const std::string &function = "",
           const std::string &className = "") {
    logImpl(level, message, fields, function, className);
  }

private:
  friend class ScopedSettings;

  template <typename T>
  void logImpl(LOGLEVEL level, const T &message, const LogFields &fields, const std::string &function,
               const std::string &className) {
    // Avoid converting a rejected value to text. Expressions passed as
    // `message` have already been evaluated by the caller; use isEnabled()
    // before the call when creating the value itself is expensive.
    OutputSettings outputSettings;
    if (!tryCaptureOutputSettings(level, outputSettings))
      return;

    std::ostringstream stream;
    stream << message;
    formatAndWriteToSinks(level, stream.str(), fields, function, className, outputSettings);
  }

  SinkHandle addSinkLocked(LoggerState &state, const std::shared_ptr<LogSink> &sink) {
    const std::size_t id = m_nextSinkId++;
    state.sinks[id]      = sink;
    return SinkHandle(this, id);
  }

  static bool containsSinkLocked(const LoggerState &state, const std::shared_ptr<LogSink> &wanted) {
    for (std::map<std::size_t, std::shared_ptr<LogSink>>::const_iterator sink = state.sinks.begin();
         sink != state.sinks.end(); ++sink) {
      if (sink->second.get() == wanted.get())
        return true;
    }
    return false;
  }

  static void copySinksLocked(const LoggerState &state, std::vector<std::shared_ptr<LogSink>> &sinks) {
    sinks.reserve(state.sinks.size());
    for (std::map<std::size_t, std::shared_ptr<LogSink>>::const_iterator sink = state.sinks.begin();
         sink != state.sinks.end(); ++sink)
      sinks.push_back(sink->second);
  }

  static std::size_t levelIndex(LOGLEVEL level) {
    return static_cast<std::size_t>(level);
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

  // The caller must hold m_state_mux. Setters and logging use the current
  // thread's top override when present, otherwise they use the global state.
  LoggerState &activeStateLocked(ThreadToken threadToken) {
    std::map<ThreadToken, std::vector<LoggerState>>::iterator stack = m_scopedStateStacks.find(threadToken);
    return stack == m_scopedStateStacks.end() || stack->second.empty() ? m_globalState : stack->second.back();
  }

  const LoggerState &activeStateLocked(ThreadToken threadToken) const {
    std::map<ThreadToken, std::vector<LoggerState>>::const_iterator stack = m_scopedStateStacks.find(threadToken);
    return stack == m_scopedStateStacks.end() || stack->second.empty() ? m_globalState : stack->second.back();
  }

  // Keep the threshold and filter rule in one place for isEnabled() and log().
  static bool acceptsLevel(const LoggerState &state, LOGLEVEL level) {
    return level <= state.logLevel && (state.filterLevels.empty() || state.filterLevels.count(level) != 0);
  }

  void beginSettingsOverride(ThreadToken threadToken) {
    std::lock_guard<std::mutex> state_lck(m_state_mux);
    std::vector<LoggerState>   &stack = m_scopedStateStacks[threadToken];
    stack.push_back(stack.empty() ? m_globalState : stack.back());
  }

  void endSettingsOverride(ThreadToken threadToken) {
    std::lock_guard<std::mutex>                               state_lck(m_state_mux);
    std::map<ThreadToken, std::vector<LoggerState>>::iterator stack = m_scopedStateStacks.find(threadToken);
    if (stack == m_scopedStateStacks.end() || stack->second.empty())
      return;

    stack->second.pop_back();
    if (stack->second.empty())
      m_scopedStateStacks.erase(stack);
  }

  // Check the level and copy the selected color, formatter, and sinks while
  // holding the state lock. Formatting and output happen after releasing it.
  bool tryCaptureOutputSettings(LOGLEVEL level, OutputSettings &outputSettings) const {
    std::lock_guard<std::mutex> state_lck(m_state_mux);
    const LoggerState          &state = activeStateLocked(getCurrentThreadToken());
    if (!acceptsLevel(state, level))
      return false;

    const std::size_t index  = levelIndex(level);
    outputSettings.color     = index < state.colors.size() ? state.colors[index] : Color::WHITE;
    outputSettings.colorMode = state.colorMode;
    outputSettings.formatter = state.formatter;
    copySinksLocked(state, outputSettings.sinks);
    return true;
  }

  void formatAndWriteToSinks(LOGLEVEL level, const std::string &message, const LogFields &fields,
                             const std::string &function, const std::string &className,
                             const OutputSettings &outputSettings) {
    LogEntry entry  = {};
    entry.level     = level;
    entry.color     = outputSettings.color;
    entry.colorMode = outputSettings.colorMode;
    entry.message   = message;
    entry.function  = function;
    entry.className = className;
    entry.fields    = fields;

    // Formatting and sink writes are serialized. This lets a formatter safely
    // keep internal state when it is used by this Logger.
    std::lock_guard<std::recursive_mutex> output_lck(m_output_mux);
    entry.timestamp = outputSettings.formatter->formatTimestamp(std::time(nullptr));
    entry.text      = outputSettings.formatter->format(entry);
    for (std::vector<std::shared_ptr<LogSink>>::const_iterator sink = outputSettings.sinks.begin();
         sink != outputSettings.sinks.end(); ++sink)
      (*sink)->write(entry);
  }

  mutable std::mutex   m_state_mux;
  std::recursive_mutex m_output_mux;
  LoggerState          m_globalState;
  std::size_t          m_nextSinkId;
  SinkHandle           m_defaultConsoleSinkHandle;

  // Unique tokens keep stacks separate even when the operating system reuses a
  // thread ID. A moved guard keeps its creating thread's token for cleanup.
  std::map<ThreadToken, std::vector<LoggerState>> m_scopedStateStacks;
};

class ScopedSettings {
public:
  explicit ScopedSettings(Logger &logger) : m_logger(&logger), m_ownerThreadToken(Logger::getCurrentThreadToken()) {
    m_logger->beginSettingsOverride(m_ownerThreadToken);
  }

  ~ScopedSettings() {
    if (m_logger)
      m_logger->endSettingsOverride(m_ownerThreadToken);
  }

  ScopedSettings(ScopedSettings &&other) : m_logger(other.m_logger), m_ownerThreadToken(other.m_ownerThreadToken) {
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
};

inline ScopedSettings Logger::scopedSettings() {
  return ScopedSettings(*this);
}

inline Logger &defaultLogger() {
  return Logger::getInstance();
}

} // namespace cppcolorlog

// Short global names keep the public API easy to use without a namespace prefix.
using ColorMode           = cppcolorlog::ColorMode;
using FileOpenMode        = cppcolorlog::FileOpenMode;
using LogFields           = cppcolorlog::LogFields;
using LogEntry            = cppcolorlog::LogEntry;
using LogFormatter        = cppcolorlog::LogFormatter;
using DefaultLogFormatter = cppcolorlog::DefaultLogFormatter;
using SinkHandle          = cppcolorlog::SinkHandle;
using LogSink             = cppcolorlog::LogSink;
using ConsoleSink         = cppcolorlog::ConsoleSink;
using FileSink            = cppcolorlog::FileSink;
using InMemorySink        = cppcolorlog::InMemorySink;
using LoggerState         = cppcolorlog::LoggerState;
using Logger              = cppcolorlog::Logger;
using ScopedSettings      = cppcolorlog::ScopedSettings;

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
 * Example: LOGGER_LOG(LOGLEVEL::INFO, "Server started");
 */
#define LOGGER_LOG(level, message)                                                                                     \
  ::cppcolorlog::defaultLogger().log(                                                                                  \
      level, message, ::cppcolorlog::detail::normalizeFunctionSignature(CPPCOLORLOG_SIGNATURE, __func__))

/**
 * Logs a message with key/value fields and automatic source context.
 * The variadic parameter allows a braced field list to contain commas.
 * Example: LOGGER_LOG_FIELDS(LOGLEVEL::INFO, "Done", {{"status", "200"}});
 */
#define LOGGER_LOG_FIELDS(level, message, ...)                                                                         \
  ::cppcolorlog::defaultLogger().log(                                                                                  \
      level, message, __VA_ARGS__, ::cppcolorlog::detail::normalizeFunctionSignature(CPPCOLORLOG_SIGNATURE, __func__))

/**
 * Logs with a name chosen by the caller. Prefer this for meaningful lambda
 * names or context text that must be identical on every compiler.
 * Example: LOGGER_LOG_WITH_CONTEXT(LOGLEVEL::INFO, "worker", "Task started");
 */
#define LOGGER_LOG_WITH_CONTEXT(level, context, message) ::cppcolorlog::defaultLogger().log(level, message, context)

#endif // CPPCOLORLOGGER_LOGGER_H
