# Colorful C++ Logger

<img src="logo.png" alt="CppColorLog Logo" width="18%">

A small, header-only C++11 logger with colored console output, level filtering,
multiple sinks, temporary settings, and thread-safe configuration and output.

## Features

- One self-contained header and a CMake interface target
- C++11 and newer
- Thread-safe settings, output dispatch, file output, and memory snapshots
- Terminal-aware and customizable console colors
- Console, file, in-memory, and user-defined sinks
- A verbosity threshold plus an optional allowed-level list
- Fast enabled-level checks for guarding expensive messages
- Structured key/value fields and customizable text formatting
- Exception-safe scoped settings
- Unified automatic function and class context on GCC, Clang, and MSVC

## Build with Make

The repository `Makefile` wraps the CMake commands:

| Action | Command |
|---|---|
| Configure and build the library | `make` |
| Configure only | `make configure` |
| Build examples | `make examples` |
| Build and run examples | `make run-examples` |
| Build and run tests | `make test` |
| Format source | `make format` |
| Install | `make install` |
| Remove compiled files from `BUILD_DIR` but keep its CMake configuration | `make clean` |
| Remove this project's complete `build/` and `build-*` directories | `make clean-all` |
| List commands | `make help` |

Build settings can be overridden, for example:

```bash
make BUILD_TYPE=Release JOBS=8
make test BUILD_DIR=build-release BUILD_TYPE=Release
```

`make clean` runs CMake's clean target for the selected `BUILD_DIR` and keeps
that directory configured. `make clean-all` removes complete `build/` and
`build-*` directories belonging to this project. Both commands validate their
targets, and `make clean BUILD_DIR=.` is rejected.

Direct CMake commands remain available:

```bash
cmake -S . -B build \
  -DCPPCOLORLOGGER_BUILD_TESTS=ON \
  -DCPPCOLORLOGGER_BUILD_EXAMPLES=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

An ordinary `make` or CMake configuration builds only the header-only library.
Tests and examples are enabled only when explicitly requested, so a normal
build does not download GoogleTest. When tests are enabled, CMake uses an
installed GoogleTest package when available and downloads it only as a
fallback.

The minimum supported CMake version is 3.14.
Builds must use a separate output directory, such as `build/`; CMake rejects
in-source builds so generated files cannot overwrite project files.

The examples are grouped by topic instead of repeating one setter per program:

| Example | Demonstrates |
|---|---|
| [`basic_logging.cpp`](examples/01_basic_logging/basic_logging.cpp) | Thresholds, levels, and automatic method context |
| [`settings.cpp`](examples/02_settings/settings.cpp) | Allowed levels, colors, and scoped overrides |
| [`sinks.cpp`](examples/03_sinks/sinks.cpp) | Console, file, and memory sink lifecycle |
| [`source_context.cpp`](examples/04_source_context/source_context.cpp) | Functions, classes, templates, operators, and lambdas |
| [`structured_formatting.cpp`](examples/05_structured_formatting/structured_formatting.cpp) | Structured fields and custom formatting |

## Basic usage

The public API uses short global names; application code does not need a
namespace prefix.

```cpp
#include "cppColorLogger/logger.h"

int main() {
  LOGGER.setLogLevel(LogLevel::Debug);

  LOGGER_LOG(LogLevel::Info, "Application started");
  LOGGER_LOG(LogLevel::Debug, "Debug details");
}
```

### Why use `LOGGER_LOG` instead of `LOG`?

`LOGGER` is the shared logger object. Use it for configuration and sink
management:

```cpp
LOGGER.setLogLevel(LogLevel::Debug);
LOGGER.addFileSink("application.log");
```

`LOGGER_LOG` is a macro so it can capture the compiler function signature at the
exact call site:

```cpp
void Service::start() {
  LOGGER_LOG(LogLevel::Info, "Service started");
}
```

```text
[2026-09-05 19:10:00] [INFO] [Service::start] Service started
```

The library intentionally does not define a shorter `LOG` macro. `LOG` is a
common global macro name used by other logging libraries, so defining it in a
public header could cause macro-redefinition errors in applications that use
more than one dependency.

Direct `Logger::log()` access is intentionally private. This keeps the public
API consistent: use `LOGGER.someMethod()` for configuration, `LOGGER_LOG` for
ordinary messages, and `LOGGER_LOG_WITH_CONTEXT` when the context must be
chosen manually.

`setLogLevel()` sets the highest verbosity that is accepted. For example,
`LogLevel::Error` accepts `Always`, `Fatal`, and `Error`, while
`LogLevel::Debug` additionally accepts `Warn`, `Info`, and `Debug`.

## Log levels and default colors

- `LogLevel::Always` — white
- `LogLevel::Fatal` — magenta
- `LogLevel::Error` — red
- `LogLevel::Warn` — yellow
- `LogLevel::Info` — green
- `LogLevel::Debug` — cyan
- `LogLevel::Verbose` — blue

`Always` always passes the verbosity threshold. Like every other level, it can
still be excluded by an active allowed-level list.

## Avoiding expensive work for disabled levels

For ordinary strings and inexpensive values, log normally:

```cpp
LOGGER_LOG(LogLevel::Info, "Application started");
```

The logger checks the threshold and allowed-level list before converting the
value with `std::ostringstream`. However, C++ creates function arguments before
calling the logger, so this expensive message is still built when `Debug` is
disabled:

```cpp
LOGGER_LOG(LogLevel::Debug, buildExpensiveDebugMessage());
```

Check the level before doing expensive work:

```cpp
if (LOGGER.isEnabled(LogLevel::Debug)) {
  LOGGER_LOG(LogLevel::Debug, buildExpensiveDebugMessage());
}
```

`isEnabled()` works in C++11 and newer. It returns a snapshot of the calling
thread's settings. If those settings change after the check, `LOGGER_LOG`
checks them again before formatting and writing the message.

## Automatic source context

Use `LOGGER_LOG` in both free functions and class methods. The logger detects
the context from the compiler-provided function signature:

```cpp
void refreshCache() {
  LOGGER_LOG(LogLevel::Info, "Refreshing cache");
}

class Service {
public:
  void start() {
    LOGGER_LOG(LogLevel::Info, "Service started");
  }
};
```

The resulting contexts are `refreshCache` and `Service::start`. Constructors,
destructors, static member functions, and operators use the same macro without
additional configuration.

| Situation | What the user should do |
|---|---|
| Free function, member, static member, constructor, destructor, or operator | Use `LOGGER_LOG`; no additional setup is needed. |
| Function or class template | Use `LOGGER_LOG`; instantiated template types are detected automatically. |
| Lambda with an acceptable `<lambda>` label | Use `LOGGER_LOG`. |
| Lambda that needs a meaningful stable name | Use `LOGGER_LOG_WITH_CONTEXT`. |
| Context must hide types or remain identical across compilers | Use `LOGGER_LOG_WITH_CONTEXT`. |
| Compiler other than GCC, Clang, or MSVC | Automatic class detection is unsupported; use `LOGGER_LOG_WITH_CONTEXT` when qualified context is needed. |
| C++20 or newer build | Use the same macros; no special configuration is needed. |

Automatic class detection is supported on GCC, Clang, and MSVC. It uses
`__PRETTY_FUNCTION__` on GCC and Clang and `__FUNCSIG__` on MSVC. Other
compilers are not supported for automatic class detection. They fall back to
standard `__func__`, which normally provides only the function name. Logging
continues to work, but use an explicit context if a class-qualified name is
required.

Lambdas have no user-defined function name, so `LOGGER_LOG` reports `<lambda>`.
Provide a stable application name when it matters:

```cpp
auto callback = [] {
  LOGGER_LOG_WITH_CONTEXT(
      LogLevel::Info,
      "RequestHandler::onResponse",
      "Callback invoked");
};
```

Templates need no special setup. C++20 builds use the same macros because the
logger intentionally retains its C++11-compatible implementation. Detailed
template, lambda, compiler-fallback, and parser examples are kept in the
[source-context guide](docs/source_context_parser.md). The complete runnable
example is
[`examples/04_source_context/source_context.cpp`](examples/04_source_context/source_context.cpp).

## File and memory sinks

```cpp
std::shared_ptr<FileSink> logger_file_sink =
    LOGGER.addFileSink("application.log", FileOpenMode::Append);

if (!logger_file_sink->isOpen())
  std::cerr << logger_file_sink->getLastError() << '\n';

std::shared_ptr<InMemorySink> memory = LOGGER.enableInMemorySink();
LOGGER_LOG(LogLevel::Info, "Stored by every active sink");

if (!LOGGER.flushAllSinks())
  std::cerr << logger_file_sink->getLastError() << '\n';

for (const std::string &entry : memory->getLogs())
  std::cout << entry << '\n';
```

Memory access returns a snapshot, so callers never retain an unlocked reference
to the sink's internal storage.

### File open modes

The default mode is `FileOpenMode::Append`, which keeps existing content and
writes new entries at the end. `FileOpenMode::Truncate` clears existing content
when the sink opens the file:

```cpp
LOGGER.addFileSink("history.log"); // Append is the default
LOGGER.addFileSink("latest.log", FileOpenMode::Truncate);
```

### Flushing and detecting file errors

#### Default behavior

The default behavior is `FileFlushMode::AfterEachEntry`. Every log entry is
flushed automatically, so the application does not need to call `flush()`:

```cpp
LOGGER.addFileSink("application.log");

LOGGER_LOG(LogLevel::Info, "First message");
// The file has already been flushed.
```

The call above is equivalent to writing:

```cpp
LOGGER.addFileSink(
    "application.log",
    FileOpenMode::Append,
    FileFlushMode::AfterEachEntry);
```

Flushing every entry reduces the amount of recent logging that could be lost if
the application stops unexpectedly.

#### Manual flushing

`FileFlushMode::Manual` does not automatically flush after each entry. Keep the
returned `FileSink` and call `flush()` when the buffered entries must be written:

```cpp
std::shared_ptr<FileSink> logger_file_sink = LOGGER.addFileSink(
    "application.log",
    FileOpenMode::Append,
    FileFlushMode::Manual);

LOGGER_LOG(LogLevel::Info, "First message");
LOGGER_LOG(LogLevel::Info, "Second message");

// Flush only this file sink.
if (!logger_file_sink->flush()) {
  std::cerr << logger_file_sink->getLastError() << '\n';
}
```

To flush every sink in the calling thread's active settings instead of one
specific file, use:

```cpp
if (!LOGGER.flushAllSinks()) {
  std::cerr << "A log sink could not be flushed\n";
}
```

In summary:

- `AfterEachEntry` flushes automatically after every message and is the
  default.
- `Manual` requires `logger_file_sink->flush()` for one file or
  `LOGGER.flushAllSinks()` for all active sinks.
- Closing a file normally writes its remaining buffered data, but an explicit
  flush is needed to check whether flushing succeeded.

Manual mode can improve throughput when many messages are written. Its tradeoff
is that recent buffered entries may be lost if the application crashes before
they are flushed. `FileOpenMode` and `FileFlushMode` are independent: the first
controls whether existing file content is kept, while the second controls when
new entries are flushed.

File errors are never silently cleared:

- `isOpen()` tells whether the file was opened.
- `hasError()` reports whether opening, writing, or flushing has failed.
- `getLastError()` returns a readable description of the first failure.
- `flush()` returns `false` after a failure.

When a write or flush fails after the file was opened, the error remains stored
and later writes are ignored. The sink does not automatically reopen the file,
because doing so could hide missing entries or write to an unexpected file. The
application can inspect the error, remove the failed sink, and add a new one.
Depending on the operating system, deleting or renaming an open file may not
cause an immediate failure because the process can still own an open file
handle. Errors are reported when the operating system rejects a write or flush.

Built-in size-based and time-based rotation are not enabled. Applications that
need rotation can provide a custom `LogSink`; each `write()` call receives one
complete log entry.

## Adding and removing sinks

`addSink()` returns a `SinkHandle`. Keep this handle when you may need to remove
that specific sink later:

```cpp
class MySink : public LogSink {
public:
  void write(const LogEntry &entry, const LogStyle &) override {
    sendSomewhere(entry.formattedText);
  }
};

std::shared_ptr<MySink> sink = std::make_shared<MySink>();
SinkHandle handle = LOGGER.addSink(sink);

LOGGER_LOG(LogLevel::Info, "Sent to MySink");
LOGGER.removeSink(handle);
LOGGER_LOG(LogLevel::Info, "MySink no longer receives this");
```

`removeSink()` returns `true` when it removes a sink. It returns `false` and
makes no changes when the handle is invalid, belongs to another logger, or has
already been removed. Removing one sink does not change the log level, filters,
colors, or other sinks.

Use `clearSinks()` to remove every active sink. This includes the default console
sink and the sink created by `enableInMemorySink()`. They can be added again:

```cpp
LOGGER.clearSinks();

SinkHandle console = LOGGER.addConsoleSink();
std::shared_ptr<InMemorySink> memory = LOGGER.enableInMemorySink();
```

The default console sink can also be removed individually:

```cpp
LOGGER.removeSink(LOGGER.getDefaultConsoleSinkHandle());
```

The file and memory convenience methods return their sink objects so callers can
inspect their status or stored entries. When an individual removal handle is
needed, construct the sink and pass it to `addSink()` directly:

```cpp
std::shared_ptr<FileSink> logger_file_sink =
    std::make_shared<FileSink>("application.log");
SinkHandle fileHandle = LOGGER.addSink(logger_file_sink);
```

Sink changes follow the same rules as other scoped settings. Removing or clearing
sinks inside `ScopedSettings` affects only that temporary settings copy; leaving
the scope restores the previous sink list.

Adding, removing, and copying the active sink list are protected by the logger's
settings mutex. A log call copies `shared_ptr`s to its selected sinks before
writing. Therefore, removing a sink while another thread is already using it is
safe: the in-progress write may finish, and the sink is destroyed only after that
write releases its copy.

## Structured logging fields

Use `LOGGER_LOG_FIELDS` when a message has values that a sink may need to inspect
separately:

```cpp
LOGGER_LOG_FIELDS(
    LogLevel::Info,
    "Request completed",
    {{"status", "200"}, {"duration_ms", "14"}});
```

When fields are written directly in the logging call, the outer `{}` contains
the complete field list and each inner `{}` contains one key/value pair. If the
fields are already stored in a `LogFields` variable, pass the variable without
additional braces:

```cpp
LogFields fields = {{"status", "200"}, {"duration_ms", "14"}};
LOGGER_LOG_FIELDS(LogLevel::Info, "Request completed", fields);
```

Use `LOGGER_LOG` instead when the message has no fields.

The console, file, and memory sinks produce readable text:

```text
[2026-09-05 14:30:12] [INFO] [handleRequest] Request completed [status=200, duration_ms=14]
```

The timestamp and automatically detected context depend on when and where the
macro is called. A normal message without fields keeps its existing format.

Keys and values are strings, and their insertion order is preserved. Keeping the
field type this small makes initializer-list use predictable in C++11.

A sink that overrides `write(const LogEntry&, const LogStyle&)` receives these
event values separately:

- `timestamp`
- `level`
- `message`
- `context`
- `fields`
- `formattedText`, containing the human-readable rendering

The project does not include a JSON sink. A future JSON sink should define rules
for duplicate keys and invalid text before adding its own serializer.

## Custom log formatting

A formatter controls how a `LogEntry` becomes readable text. A sink controls
where that text is written. This separation lets the same formatter work with
the existing console, file, and memory sinks.

The built-in `DefaultLogFormatter` keeps the standard output unchanged:

```text
[2026-09-05 14:30:12] [INFO] [handleRequest] Request completed [status=200]
```

The smallest custom formatter only needs to override `format()`:

```cpp
class SimpleLogFormatter : public LogFormatter {
public:
  std::string format(const LogEntry &entry) const override {
    return std::string(logLevelToString(entry.level)) + ": " + entry.message;
  }
};
```

Install it without changing or replacing any sinks:

```cpp
LOGGER.setFormatter(std::make_shared<SimpleLogFormatter>());
LOGGER_LOG(LogLevel::Info, "Request completed");
```

Example output:

```text
INFO: Request completed
```

`format()` receives the raw level, message, source context, and fields, so it can
choose which values to include and how to display them.

Changing only the timestamp is also straightforward. Inherit from the default
formatter to keep the standard line layout, then override `formatTimestamp()`:

```cpp
class TimeOnlyLogFormatter : public DefaultLogFormatter {
public:
  std::string formatTimestamp(std::time_t entryTime) const override {
    return formatTimeWithPattern(entryTime, "%H:%M:%S");
  }
};
```

`formatTimeWithPattern()` accepts the same placeholders as `std::strftime`.
For example, `%H:%M:%S` produces a timestamp such as `14:30:12`.

A formatter should return plain text. Destination-specific behavior remains in
the sinks; for example, `ConsoleSink` adds color after formatting, while
`FileSink` writes the same text without ANSI color codes.

Return to the standard layout with:

```cpp
LOGGER.useDefaultFormatter();
```

The complete runnable
[`examples/05_structured_formatting/structured_formatting.cpp`](examples/05_structured_formatting/structured_formatting.cpp)
demonstrates custom timestamp, level, source context, and field rendering.

Formatter changes follow the same thread-local `ScopedSettings` behavior as the
other logger settings. A settings mutex protects formatter replacement, and each
log call keeps its selected formatter alive until that message finishes. Calls
to the formatter and sinks are serialized. A formatter can therefore be changed
while other threads are logging through the same `Logger`.

## Allowing only selected levels

The verbosity threshold and allowed-level list are both applied:

```cpp
LOGGER.setLogLevel(LogLevel::Debug);
LOGGER.setAllowedLevels({LogLevel::Error, LogLevel::Warn});

LOGGER_LOG(LogLevel::Debug, "Filtered out");
LOGGER_LOG(LogLevel::Error, "Allowed");

LOGGER.clearAllowedLevels();
```

## Custom colors

```cpp
LOGGER.setLevelColor(LogLevel::Info, Color::Cyan);
LOGGER_LOG(LogLevel::Info, "Cyan console message");
```

Colors are owned as strings by the logger. File and memory sinks receive plain
text without ANSI escape sequences.

By default, `ConsoleSink` adds ANSI colors only when standard output is an
interactive terminal. Redirected output, such as a file or pipe, stays plain.
On Windows, the logger enables virtual-terminal processing when the console
supports it.

A non-empty `NO_COLOR` environment variable disables colors in automatic mode:

```bash
NO_COLOR=1 ./your_application
```

Applications can override automatic detection:

```cpp
LOGGER.setColorMode(ColorMode::Disabled); // Always use plain console output
LOGGER.setColorMode(ColorMode::Enabled);  // Always include ANSI colors
LOGGER.setColorMode(ColorMode::Automatic);    // Detect the terminal and respect NO_COLOR
```

The logger stores this choice as a `ColorMode`:

| Mode | How to select it | Behaviour |
| --- | --- | --- |
| `ColorMode::Automatic` | Default, or call `setColorMode(ColorMode::Automatic)` | Uses color for a supported interactive terminal. Uses plain text for redirected output or when `NO_COLOR` is set. |
| `ColorMode::Enabled` | Call `setColorMode(ColorMode::Enabled)` | Always adds ANSI color codes, even when output is redirected or `NO_COLOR` is set. |
| `ColorMode::Disabled` | Call `setColorMode(ColorMode::Disabled)` | Never adds ANSI color codes. |

Most applications should keep the default `Automatic` mode. Use `Enabled` only
when the destination is known to understand ANSI codes. Otherwise, redirected
logs may contain visible escape characters. Use `Disabled` when plain output is
always required.

### How automatic terminal detection works

In `Automatic` mode, the logger checks standard output (`stdout`) before adding
ANSI color codes:

- On Linux and macOS, it calls `isatty(STDOUT_FILENO)`. A true result means
  stdout is connected to an interactive terminal, which the logger assumes can
  display ANSI colors. Redirection to a file or pipe normally returns false.
- On Windows, it calls `GetStdHandle(STD_OUTPUT_HANDLE)` and `GetConsoleMode()`
  to verify that stdout is a console. It then uses `SetConsoleMode()` to enable
  virtual-terminal processing. Automatic color is used only when these checks
  succeed.
- On other platforms, automatic color is disabled because terminal support
  cannot be confirmed.

After detecting the terminal, the logger checks `NO_COLOR`. A non-empty value
turns color off in `Automatic` mode even when stdout is an interactive terminal.

Explicit `Enabled` or `Disabled` settings take priority over `NO_COLOR` and
terminal detection. These settings also work with `ScopedSettings`, so temporary
changes are restored automatically.

## Temporary settings

Prefer the scoped API. It restores the level, allowed-level list, colors, sinks,
memory sink, and formatter even when code exits early or throws an exception:

```cpp
{
  ScopedSettings temporary = LOGGER.scopedSettings();
  LOGGER.setLogLevel(LogLevel::Error);
  LOGGER.setAllowedLevels({LogLevel::Error});
  LOGGER_LOG(LogLevel::Error, "Temporary configuration");
}
```

The configuration rules are:

- Outside a settings scope, setters update the global logger configuration.
- Inside a settings scope, setters update only the calling thread's temporary
  configuration.
- A thread entering its first scope copies the current global configuration.
- A nested scope copies that thread's current temporary configuration.
- Leaving a scope restores the previous configuration for that thread only.
- A global change made by another thread is visible after the local scope ends,
  but it does not replace the local scope's existing snapshot.

Internally, the logger assigns each thread a unique numeric token the first time
that thread uses a logger. Unlike `std::thread::id`, this token is not reused
after the thread exits. The token selects the thread's temporary settings stack.
In simplified form, the lookup works like this:

```cpp
Settings &activeSettingsForCurrentThread() {
  const ThreadToken threadToken = getCurrentThreadToken();
  const auto stack = m_scopedSettingsStacks.find(threadToken);

  if (stack == m_scopedSettingsStacks.end() || stack->second.empty())
    return m_globalSettings;

  return stack->second.back().m_settings;
}
```

`ScopedSettings` remembers its creating thread's token. If the scope is moved to
another thread, its destructor still removes the correct settings stack. A new
thread always receives a different token, so it cannot accidentally inherit a
stack left alive by an older thread. The token provides identity, not
synchronization; a mutex still protects the shared map while stacks are read,
added, updated, or removed.

Each scope also has its own ID. Cleanup removes that exact scope instead of
blindly removing the top item. This matters when an outer scope is moved and
destroyed before a nested scope. The nested scope remains active until its own
guard is destroyed. A `Logger` must outlive every `ScopedSettings` guard created
from it.

Consequently, temporary settings can safely overlap:

```cpp
#include <thread>

std::thread restrictiveWorker([] {
  ScopedSettings temporary = LOGGER.scopedSettings();
  LOGGER.setLogLevel(LogLevel::Error);
  LOGGER_LOG(LogLevel::Info, "Hidden only in this thread");
});

std::thread verboseWorker([] {
  ScopedSettings temporary = LOGGER.scopedSettings();
  LOGGER.setLogLevel(LogLevel::Debug);
  LOGGER_LOG(LogLevel::Debug, "Visible in this thread");
});

restrictiveWorker.join();
verboseWorker.join();
```

## Custom sinks

Implement `write(const LogEntry&, const LogStyle&)`. Use
`entry.formattedText` when the sink only needs the fully formatted line:

```cpp
class MySink : public LogSink {
public:
  void write(const LogEntry &entry, const LogStyle &) override {
    sendSomewhere(entry.formattedText);
  }
};

LOGGER.addSink(std::make_shared<MySink>());
```

The entry also provides the timestamp, level, original message, source context,
and fields. The separate `LogStyle` argument contains the selected console color
and color mode. Non-console sinks can ignore it.

If a custom formatter or sink throws an exception, the logger catches it.
A failing sink does not prevent later sinks from receiving the same entry. Check
and clear the logger-level status like this:

```cpp
if (LOGGER.hasError()) {
  std::cerr << LOGGER.getLastError() << '\n';
  LOGGER.clearError();
}
```

`FileSink::getLastError()` reports file open, write, and flush failures for one
file. `Logger::getLastError()` reports failures thrown by custom formatters or
sinks. The logger also stops a recursive logging chain after eight nested calls.
This protects an application when a custom sink accidentally calls the same
logger from its `write()` method.

## CMake integration

```cmake
add_subdirectory(path/to/CppColorLogger)
target_link_libraries(your_target PRIVATE CppColorLogger::cppColorLogger)
```

Tests and examples are disabled by default, including when the library is added
to a parent project. They can be enabled independently when working on the
logger itself:

```bash
cmake -S . -B build-tests -DCPPCOLORLOGGER_BUILD_TESTS=ON
cmake -S . -B build-examples -DCPPCOLORLOGGER_BUILD_EXAMPLES=ON
```

`BUILD_TESTING=OFF` is also respected as CMake's standard project-wide switch.
For example, tests remain disabled when both of these options are supplied:

```bash
cmake -S . -B build \
  -DCPPCOLORLOGGER_BUILD_TESTS=ON \
  -DBUILD_TESTING=OFF
```

Then include:

```cpp
#include "cppColorLogger/logger.h"
```

The install step exports the header and CMake target.

## Logging macros

Use `LOGGER_LOG` for normal logging. Use `LOGGER_LOG_WITH_CONTEXT` when you need
to provide a stable context name, such as for a lambda. `LogLevel` is the single
public log-level type name.

## Thread safety

The global configuration and all per-thread temporary-setting stacks are
protected by a state mutex. Logging copies the active thread's configuration
while holding that mutex, then releases it before output I/O. Output dispatch is
serialized to keep complete entries ordered across sinks. File and memory sinks
also protect their own state. Memory logs are returned by value as safe
snapshots.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE).
