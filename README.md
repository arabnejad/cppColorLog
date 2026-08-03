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
- Automatic function and class context
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
| Clean compiled outputs | `make clean` |
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

  LOGGER_F(LogLevel::INFO, "Application started");
  LOGGER_F(LogLevel::DEBUG, "Debug details");
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

## Logging from a class

`LOGGER_C` adds the class and method name. `LOGGER_F` adds the function name.

```cpp
class Service {
public:
  void start() {
    LOGGER_C(LogLevel::INFO, "Service started");
  }
};
```

On GCC and Clang, class names are demangled. Other compilers use the name
provided by `typeid` without depending on the non-portable `<cxxabi.h>` API.

## File and memory sinks

```cpp
LOGGER.addFileSink("application.log");

std::shared_ptr<InMemorySink> memory = LOGGER.enableInMemorySink();
LOGGER_F(LogLevel::INFO, "Stored by every active sink");

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

LOGGER_F(LogLevel::DEBUG, "Filtered out");
LOGGER_F(LogLevel::ERROR, "Allowed");

LOGGER.clearFilterLevels();
```

## Custom colors

```cpp
LOGGER.setLevelColor(LogLevel::INFO, Color::CYAN);
LOGGER_F(LogLevel::INFO, "Cyan console message");
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
  LOGGER_F(LogLevel::ERROR, "Temporary configuration");
}
```

`pushLogSetting()` and `popLogSetting()` remain available for compatibility.
Settings affect the process-wide default logger, so overlapping settings scopes
should be coordinated by the application.

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

New code should use `LogLevel`.

## Thread safety

Configuration is copied under a state mutex, then released before output I/O.
Output dispatch is serialized to keep complete entries ordered across sinks.
File and memory sinks also protect their own state. Memory logs are returned by
value as safe snapshots.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE).
