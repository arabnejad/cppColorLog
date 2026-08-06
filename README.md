# Colorful C++ Logger

<img src="logo.png" alt="CppColorLog Logo" width="18%">

A small, header-only C++11 logger with colored console output, level filtering,
multiple sinks, temporary settings, and thread-safe configuration and output.

## Features

- One header and a CMake interface target
- C++11 and newer
- Thread-safe settings, output dispatch, file output, and memory snapshots
- Customizable console colors
- Console, file, in-memory, and user-defined sinks
- Threshold and whitelist filtering
- Exception-safe scoped settings, plus compatible push/pop methods
- Unified automatic function and class context on GCC, Clang, and MSVC
- Portable time formatting and guarded GNU demangling

## Build with Make

The repository `Makefile` wraps the CMake commands:

| Action | Command |
|---|---|
| Configure and build everything | `make` |
| Configure only | `make configure` |
| Build examples | `make examples` |
| Build and run examples | `make run-examples` |
| Build tests | `make tests` |
| Build and run tests | `make test` |
| Format source | `make format` |
| Install | `make install` |
| Remove this project's `build/` and `build-*` directories | `make clean` |
| List commands | `make help` |

Build settings can be overridden, for example:

```bash
make BUILD_TYPE=Release JOBS=8
make test BUILD_DIR=build-release BUILD_TYPE=Release
```

Direct CMake commands remain available:

```bash
cmake -S . -B build -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Basic usage

```cpp
#include "cppColorLogger/logger.h"

int main() {
  LOGGER.setLogLevel(LogLevel::DEBUG);

  LOGGER_LOG(LogLevel::INFO, "Application started");
  LOGGER_LOG(LogLevel::DEBUG, "Debug details");
}
```

`setLogLevel()` sets the highest verbosity that is accepted. For example,
`ERROR` accepts `ALWAYS`, `FATAL`, and `ERROR`, while `DEBUG` additionally
accepts `WARN`, `INFO`, and `DEBUG`.

## Log levels and default colors

- `ALWAYS` — white
- `FATAL` — magenta
- `ERROR` — red
- `WARN` — yellow
- `INFO` — green
- `DEBUG` — cyan
- `VERBOSE` — blue

`ALWAYS` always passes the verbosity threshold. Like every other level, it can
still be excluded by an active whitelist filter.

## Automatic source context

Use `LOGGER_LOG` in both free functions and class methods. The logger detects
the context from the compiler-provided function signature:

```cpp
void refreshCache() {
  LOGGER_LOG(LogLevel::INFO, "Refreshing cache");
}

class Service {
public:
  void start() {
    LOGGER_LOG(LogLevel::INFO, "Service started");
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

### Lambdas

A lambda has no user-defined function name. Assigning it to a variable does not
make that variable name available to the logger. `LOGGER_LOG` therefore uses a
stable `<lambda>` context:

```cpp
auto callback = [] {
  LOGGER_LOG(LogLevel::INFO, "Callback invoked");
};
```

For a meaningful application-specific name, use
`LOGGER_LOG_WITH_CONTEXT(level, context, message)`:

```cpp
auto callback = [] {
  LOGGER_LOG_WITH_CONTEXT(
      LogLevel::INFO,
      "RequestHandler::onResponse",
      "Callback invoked");
};
```

Use the explicit form whenever context text must remain stable across compilers
or must not expose generated type names. The context argument can be a string
literal or a `std::string`.

### Function templates

Templates normally require no special handling:

```cpp
template <typename T>
void process(const T &value) {
  LOGGER_LOG(LogLevel::DEBUG, value);
}

process(42);
```

The compiler instantiates `process<int>`, and the logger normalizes the
compiler signature to that context automatically.

Member-function templates work the same way:

```cpp
class Serializer {
public:
  template <typename T>
  void write(const T &value) {
    LOGGER_LOG(LogLevel::DEBUG, value);
  }
};
```

For `serializer.write(42)`, the context resembles
`Serializer::write<int>`. A member of a class template, such as
`Repository<User>::save`, retains its instantiated class type. Use
`LOGGER_LOG_WITH_CONTEXT` when the type name is sensitive, excessively long, or
must be identical across compilers:

```cpp
LOGGER_LOG_WITH_CONTEXT(
    LogLevel::INFO,
    "UserRepository::save",
    "Saving user");
```

### C++20 and newer

The logger does not depend on `std::source_location`. C++20 builds use the same
compiler-signature implementation and the same macros as C++11 builds. No
different configuration or call style is required.

Developers who want to understand or change signature parsing can read the
[beginner-friendly source-context guide](docs/source_context_parser.md). It
first shows how to use the logger, then explains each internal helper with
step-by-step examples. The complete runnable example is
[`examples/15_source_context/15_source_context.cpp`](examples/15_source_context/15_source_context.cpp).

## File and memory sinks

```cpp
LOGGER.addFileSink("application.log");

std::shared_ptr<InMemorySink> memory = LOGGER.enableInMemorySink();
LOGGER_LOG(LogLevel::INFO, "Stored by every active sink");

for (const std::string &entry : memory->getLogs())
  std::cout << entry << '\n';
```

`setFileOutput()` remains available as a compatibility alias for
`addFileSink()`. Both append a sink; they do not replace existing sinks.

Memory access returns a snapshot, so callers never retain an unlocked reference
to the sink's internal storage.

## Filtering selected levels

The threshold and whitelist are both applied:

```cpp
LOGGER.setLogLevel(LogLevel::DEBUG);
LOGGER.setFilterLevels({LogLevel::ERROR, LogLevel::WARN});

LOGGER_LOG(LogLevel::DEBUG, "Filtered out");
LOGGER_LOG(LogLevel::ERROR, "Allowed");

LOGGER.clearFilterLevels();
```

## Custom colors

```cpp
LOGGER.setLevelColor(LogLevel::INFO, Color::CYAN);
LOGGER_LOG(LogLevel::INFO, "Cyan console message");
```

Colors are owned as strings by the logger. File and memory sinks receive plain
text without ANSI escape sequences.

## Temporary settings

Prefer the scoped API. It restores the level, filter, colors, sinks, and memory
sink even when code exits early or throws an exception:

```cpp
{
  ScopedSettings temporary = LOGGER.scopedSettings();
  LOGGER.setLogLevel(LogLevel::ERROR);
  LOGGER.setFilterLevels({LogLevel::ERROR});
  LOGGER_LOG(LogLevel::ERROR, "Temporary configuration");
}
```

`pushLogSetting()` and `popLogSetting()` remain available for compatibility.
Their stacks, like `ScopedSettings`, are independent for each thread.
When using them directly, call both methods on the same thread and balance every
push with one pop. Prefer `ScopedSettings`, which does this automatically.

The configuration rules are:

- Outside a settings scope, setters update the global logger configuration.
- Inside a settings scope, setters update only the calling thread's temporary
  configuration.
- A thread entering its first scope copies the current global configuration.
- A nested scope copies that thread's current temporary configuration.
- Leaving a scope restores the previous configuration for that thread only.
- A global change made by another thread is visible after the local scope ends,
  but it does not replace the local scope's existing snapshot.

Internally, the logger calls `std::this_thread::get_id()` to identify the
calling thread. That thread ID is used as the key for selecting its temporary
settings stack. In simplified form, the lookup works like this:

```cpp
LoggerState &activeStateForCurrentThread() {
  const std::thread::id threadId = std::this_thread::get_id();
  const auto stack = scopedStateStacks_.find(threadId);

  if (stack == scopedStateStacks_.end() || stack->second.empty())
    return globalState_;

  return stack->second.back();
}
```

The thread ID provides separation between threads; it does not provide
synchronization. A mutex protects the shared map while stacks are read, added,
updated, or removed.

Consequently, temporary settings can safely overlap:

```cpp
#include <thread>

std::thread restrictiveWorker([] {
  ScopedSettings temporary = LOGGER.scopedSettings();
  LOGGER.setLogLevel(LogLevel::ERROR);
  LOGGER_LOG(LogLevel::INFO, "Hidden only in this thread");
});

std::thread verboseWorker([] {
  ScopedSettings temporary = LOGGER.scopedSettings();
  LOGGER.setLogLevel(LogLevel::DEBUG);
  LOGGER_LOG(LogLevel::DEBUG, "Visible in this thread");
});

restrictiveWorker.join();
verboseWorker.join();
```

## Custom sinks

Implement the string method for a simple sink:

```cpp
class MySink : public LogSink {
public:
  void write(const std::string &message) override {
    // Store or send the plain formatted message.
  }
};

LOGGER.addSink(std::make_shared<MySink>());
```

A sink that needs the level or selected color can additionally override
`write(const LogEntry&)`. The logger passes structured entries directly; sinks
do not parse formatted text to recover the level.

## Isolated logger instances

Macros use the process-wide default logger. Tests and independent components
can construct a separate logger instead:

```cpp
Logger logger(false); // false means no default console sink
logger.addSink(std::make_shared<MySink>());
logger.log(LogLevel::INFO, "message", "functionName");
```

Messages can be strings or any value supported by `operator<<`.

## CMake integration

```cmake
add_subdirectory(path/to/CppColorLogger)
target_link_libraries(your_target PRIVATE CppColorLogger::cppColorLogger)
```

Then include:

```cpp
#include "cppColorLogger/logger.h"
```

The install step exports the header and CMake target.

## Compatibility

Existing code using the original misspelled `LOGLEVELL` name still compiles:

```cpp
LOGGER_F(LOGLEVELL::INFO, "Compatible with the original API");
```

`LOGGER_F` and `LOGGER_C` also remain available for source compatibility. New
code should use `LogLevel` and `LOGGER_LOG`, with
`LOGGER_LOG_WITH_CONTEXT` only when automatic context is unsuitable.

## Thread safety

The global configuration and all per-thread temporary-setting stacks are
protected by a state mutex. Logging copies the active thread's configuration
while holding that mutex, then releases it before output I/O. Output dispatch is
serialized to keep complete entries ordered across sinks. File and memory sinks
also protect their own state. Memory logs are returned by value as safe
snapshots.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE).
